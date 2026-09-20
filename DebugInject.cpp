/*
  DebugInject.cpp - serial test commands
  (part of WaveshareMon, GPL v3, see LICENSE; derived from M5Stack_xDripMon)

  Copyright (C) 2026 Patrick Sonnerat
*/
#include "DebugInject.h"
#include "Version.h"
#include "Board.h"
void enterSetupMode(bool timed);   // WaveshareMon.ino
void powerOff();                   // WaveshareMon.ino
#include "GlucoseState.h"
#include "TimeService.h"
#include "AppConfig.h"
#include "Alarms.h"
#include "Battery.h"
#include "BleObbClient.h"
#include "BleMiBand.h"
#include "BleXdrip4iOS.h"
#include "BleSetupServer.h"
#include "OtaUpdate.h"
#include "WebSetup.h"
#include "WifiService.h"
#include "NightscoutClient.h"
#include "DexcomShareClient.h"
#include "LibreLinkUpClient.h"
#include "EpdUi.h"
#include "PowerCycle.h"
#include "Log.h"
#include <Arduino.h>
#include <WiFi.h>
#include <NimBLEDevice.h>
#include <nvs_flash.h>
#if CONFIG_IDF_TARGET_ESP32S3
#include <soc/rtc_cntl_reg.h>
#endif

static void printCfg() {
  Serial.printf("[cfg] name=%s src=%u units=%u ssid=%s pass=%s url=%s token=%s tz=%s/%ld\n",
                cfg.name(), cfg.source, cfg.units, cfg.wifiSsid, cfg.wifiPass[0] ? "***" : "",
                cfg.nsUrl, cfg.nsToken[0] ? "***" : "", cfg.tzString, (long)cfg.tzOffsetSec);
  Serial.printf("[cfg] dexcom user=%s pass=%s region=%u | libre user=%s pass=%s region='%s' ver=%s | tls=%u\n",
                cfg.dxUser, cfg.dxPass[0] ? "***" : "", cfg.dxRegion, cfg.llUser,
                cfg.llPass[0] ? "***" : "", cfg.llRegion, cfg.llVersion, cfg.tlsVerify);
  Serial.printf("[cfg] colours y%u-%u r%u-%u alarms en=%u w%u-%u a%u-%u noread=%u vol %u/%u rep=%u snooze=%u sline=%u\n",
                cfg.yellowLow, cfg.yellowHigh, cfg.redLow, cfg.redHigh, cfg.alarmsEnabled,
                cfg.warnLow, cfg.warnHigh, cfg.alarmLow, cfg.alarmHigh, cfg.noReadingsMin,
                cfg.warnVolume, cfg.alarmVolume, cfg.alarmRepeatMin, cfg.snoozeMin, cfg.obbStatusLine);
}

static bool setKey(const char *key, const char *val) {
  long n = atol(val);
  if      (!strcmp(key, "src"))   cfg.source = (n < 0 || n > SRC_MAX) ? SRC_OBB : (uint8_t)n;   // reboot to apply
  else if (!strcmp(key, "units")) cfg.units = n ? UNITS_MMOL : UNITS_MGDL;
  else if (!strcmp(key, "ssid"))  strlcpy(cfg.wifiSsid, val, sizeof(cfg.wifiSsid));
  else if (!strcmp(key, "pass"))  strlcpy(cfg.wifiPass, val, sizeof(cfg.wifiPass));
  else if (!strcmp(key, "url"))   strlcpy(cfg.nsUrl, val, sizeof(cfg.nsUrl));
  else if (!strcmp(key, "token")) strlcpy(cfg.nsToken, val, sizeof(cfg.nsToken));
  else if (!strcmp(key, "dxuser")) { strlcpy(cfg.dxUser, val, sizeof(cfg.dxUser)); dxForgetSession(); }
  else if (!strcmp(key, "dxpass")) { strlcpy(cfg.dxPass, val, sizeof(cfg.dxPass)); dxForgetSession(); }
  else if (!strcmp(key, "dxreg"))  { cfg.dxRegion = (n < 0 || n > 2) ? 1 : (uint8_t)n; dxForgetSession(); }
  else if (!strcmp(key, "lluser")) { strlcpy(cfg.llUser, val, sizeof(cfg.llUser)); llForgetSession(); }
  else if (!strcmp(key, "llpass")) { strlcpy(cfg.llPass, val, sizeof(cfg.llPass)); llForgetSession(); }
  else if (!strcmp(key, "llreg"))  { strlcpy(cfg.llRegion, val, sizeof(cfg.llRegion)); llForgetSession(); }
  else if (!strcmp(key, "llver"))  strlcpy(cfg.llVersion, val, sizeof(cfg.llVersion));
  else if (!strcmp(key, "tlsv"))   cfg.tlsVerify = n ? 1 : 0;
  else if (!strcmp(key, "tz"))    strlcpy(cfg.tzString, val, sizeof(cfg.tzString));
  else if (!strcmp(key, "name"))  strlcpy(cfg.deviceName, val, sizeof(cfg.deviceName));
  else if (!strcmp(key, "ylo"))   cfg.yellowLow = n;
  else if (!strcmp(key, "yhi"))   cfg.yellowHigh = n;
  else if (!strcmp(key, "rlo"))   cfg.redLow = n;
  else if (!strcmp(key, "rhi"))   cfg.redHigh = n;
  else if (!strcmp(key, "aen"))   cfg.alarmsEnabled = n ? 1 : 0;
  else if (!strcmp(key, "wlo"))   cfg.warnLow = n;
  else if (!strcmp(key, "alo"))   cfg.alarmLow = n;
  else if (!strcmp(key, "whi"))   cfg.warnHigh = n;
  else if (!strcmp(key, "ahi"))   cfg.alarmHigh = n;
  else if (!strcmp(key, "nor"))   cfg.noReadingsMin = n;
  else if (!strcmp(key, "wvol"))  cfg.warnVolume = n;
  else if (!strcmp(key, "avol"))  cfg.alarmVolume = n;
  else if (!strcmp(key, "arep"))  cfg.alarmRepeatMin = n;
  else if (!strcmp(key, "snoz"))  cfg.snoozeMin = n;
  else if (!strcmp(key, "t24"))   cfg.timeFormat24 = n ? 1 : 0;
  else if (!strcmp(key, "dmy"))   cfg.dateFormatDMY = n ? 1 : 0;
  else if (!strcmp(key, "sline")) cfg.obbStatusLine = n ? 1 : 0;
  else if (!strcmp(key, "dbg"))   cfg.debugLog = n ? 1 : 0;
  else if (!strcmp(key, "nosleep")) cfg.noSleep = n ? 1 : 0;      // stay in the always-on loop
  else if (!strcmp(key, "sc"))    cfg.bleSecureConn = n ? 1 : 0;   // takes effect after reboot
  else return false;
  cfg.save();
  cfg.markConfigured();
  return true;
}

void debugInjectPoll() {
  static char line[192];
  static size_t n = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c != '\n') {
      if (n < sizeof(line) - 1) line[n++] = c;
      continue;
    }
    line[n] = 0;
    n = 0;

    if (strncmp(line, "bg ", 3) == 0) {
      int mgdl = 0, angle = ARROW_HIDDEN;
      sscanf(line + 3, "%d %d", &mgdl, &angle);
      time_t now = time(nullptr);
      gs.onReading((uint16_t)mgdl, now > 1600000000 ? now : 0, angle);
      Serial.printf("[dbg] injected %d mg/dL angle %d\n", mgdl, angle);
    } else if (strcmp(line, "demo") == 0) {
      static const uint16_t curve[] =
        {110, 118, 130, 145, 160, 172, 165, 150, 132, 120, 114, 108, 104, 101, 99, 98};
      gs.replaceHistory(curve, sizeof(curve) / sizeof(curve[0]));
      time_t now = time(nullptr);
      gs.onReading(96, now > 1600000000 ? now : 0, 45);
      gs.setDelta(-2, true);
      Serial.println("[dbg] demo curve injected");
    } else if (strncmp(line, "time ", 5) == 0) {
      int h = 0, m = 0;
      sscanf(line + 5, "%d %d", &h, &m);
      struct tm lt; time_t now = time(nullptr); localtime_r(&now, &lt);
      int y = now > 1600000000 ? lt.tm_year + 1900 : 2026;
      int mo = now > 1600000000 ? lt.tm_mon + 1 : 1;
      int d = now > 1600000000 ? lt.tm_mday : 15;
      timeService.setManual(y, mo, d, h, m);
      Serial.printf("[dbg] time set to %02d:%02d\n", h, m);
    } else if (strcmp(line, "status") == 0) {
      Serial.printf("[dbg] mgdl=%u angle=%d minAgo=%d delta=%d hist=%u live=%d stale=%d\n",
                    gs.mgdl, gs.arrowAngle, gs.minutesAgo(), gs.deltaMgdl, gs.histCount,
                    gs.live, gs.isStale());
      Serial.printf("[dbg] obb=%s wifi=%s ip=%s ns_err=%d setup=%d bat=%d%% %dmV alarm=%s heap=%u\n",
                    obbStateName(), wifiStateName(), wifiIp(), nsLastError(),
                    setupServerAdvertising(), battery.percent(), battery.millivolts(),
                    alarms.label(), (unsigned)ESP.getFreeHeap());
      Serial.printf("[dbg] wake=%s #%lu awake=%d nosleep=%u firstRun=%d status='%s' miband=%s key=%u dx='%s' llu='%s'\n",
                    cycleWakeName(), (unsigned long)cycleWakes(), cycleAwake(), cfg.noSleep,
                    cfg.firstRun, cycleStatusText(), miBandStateName(), cfg.mibandKeySet,
                    dxStatus(), llStatus());
      Serial.printf("[dbg] build=%lu ota='%s' server=%lu x4i=%s pw=%s\n", (unsigned long)WSMON_BUILD, otaStatus(),
                    (unsigned long)otaLatestBuild(), xdrip4iosStateName(), cfg.x4iPassword[0] ? "set" : "none");
      Serial.printf("[dbg] board=%s panel=%s wifimode=%d ap=%s/%s apip=%s apclients=%d web=%d pin=%06lu\n", BOARD_NAME,
                    ui.panelOk() ? "ok" : "MISMATCH", (int)WiFi.getMode(), wifiApActive() ? "on" : "off",
                    wifiApUp() ? "up" : "down", WiFi.softAPIP().toString().c_str(), (int)WiFi.softAPgetStationNum(),
                    webSetupActive(), (unsigned long)setupPasskey());
      if (NimBLEDevice::isInitialized()) {
        int nb = NimBLEDevice::getNumBonds();
        Serial.printf("[dbg] bonds=%d", nb);
        for (int i = 0; i < nb; i++) { NimBLEAddress b = NimBLEDevice::getBondedAddress(i); Serial.printf(" %s/t%d", b.toString().c_str(), b.getType()); }
        Serial.println();
      } else Serial.println("[dbg] ble off");
    } else if (strcmp(line, "cfg") == 0) {
      printCfg();
    } else if (strncmp(line, "set ", 4) == 0) {
      char *key = line + 4;
      char *val = strchr(key, ' ');
      if (val) { *val++ = 0; } else val = (char *)"";
      if (setKey(key, val)) {
        Serial.printf("[dbg] %s set\n", key);
        timeService.applyTz();
        wifiApplyConfig();
        ui.requestRedraw();
      } else Serial.printf("[dbg] unknown key %s\n", key);
    } else if (strncmp(line, "setup", 5) == 0) {
      bool on = strstr(line, "off") == nullptr;
      if (on) enterSetupMode(true); else setupServerAdvertise(false);
    } else if (strcmp(line, "refresh") == 0) {
      ui.requestRedraw();
    } else if (strcmp(line, "warn") == 0) {
      alarms.testSound(false);
    } else if (strcmp(line, "alarm") == 0) {
      alarms.testSound(true);
    } else if (strcmp(line, "snooze") == 0) {
      alarms.snooze();
    } else if (strcmp(line, "ns") == 0) {
      nsRequestNow();
    } else if (strncmp(line, "update", 6) == 0) {
      // "update": fetch update.inf from the repository and install a newer build;
      // "update check": only report
      otaRequest(strstr(line, "check") == nullptr);
    } else if (strcmp(line, "x4iforget") == 0) {
      xdrip4iosForgetPassword();
    } else if (strcmp(line, "wifiscan") == 0) {
      wifiScanStart();       // the networks the radio sees (2.4 GHz), printed when done
    } else if (strcmp(line, "dx") == 0) {
      dxRequestNow();
    } else if (strcmp(line, "llu") == 0) {
      llRequestNow();
    } else if (strcmp(line, "sleep") == 0) {
      // end the awake period / radio window now and deep-sleep until the next reading
      if (setupServerAdvertising()) setupServerAdvertise(false);
      cfg.noSleep = 0;
      cycleSleepNow();
    } else if (strcmp(line, "btn") == 0) {
      // watch both physical buttons for 20 s and log every edge, to map the
      // labels (PWR / BOOT) to GPIOs and find their idle / pressed levels
      pinMode(PIN_PWR_BTN, INPUT_PULLUP);
      int b = digitalRead(PIN_BOOT_BTN), p = digitalRead(PIN_PWR_BTN);
      Serial.printf("[dbg] btn watch 20s - press each button. idle BOOT(gpio0)=%d PWR(gpio18)=%d\n", b, p);
      uint32_t end = millis() + 20000;
      while (millis() < end) {
        int nb = digitalRead(PIN_BOOT_BTN), np = digitalRead(PIN_PWR_BTN);
        if (nb != b) { Serial.printf("[dbg] BOOT %d->%d @%lums\n", b, nb, (unsigned long)millis()); b = nb; }
        if (np != p) { Serial.printf("[dbg] PWR  %d->%d @%lums\n", p, np, (unsigned long)millis()); p = np; }
        delay(5);
      }
      Serial.println("[dbg] btn watch done");
    } else if (strcmp(line, "poweroff") == 0) {
      powerOff();          // deep sleep; same as holding PWR (drops USB)
    } else if (strcmp(line, "reboot") == 0) {
      ESP.restart();
    } else if (strcmp(line, "dfu") == 0) {
      // reboot into the ROM download mode (no BOOT button needed for esptool)
      Serial.println("[dbg] entering download mode");
      delay(100);
#if CONFIG_IDF_TARGET_ESP32S3
      REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
      esp_restart();
#else
      Serial.println("[dbg] dfu: not on this chip - hold BOOT while plugging the cable in");
#endif
    } else if (strcmp(line, "rtc") == 0) {
      time_t u = 0;
      bool ok = timeService.readRtc(u);
      Serial.printf("[dbg] rtc %s utc=%ld sys=%ld epd_pwr=%d\n", ok ? "ok" : "NOT READABLE",
                    (long)u, (long)time(nullptr), digitalRead(PIN_EPD_PWR));
    } else if (strcmp(line, "mbforget") == 0) {
      miBandForgetKey();
    } else if (strcmp(line, "unbond") == 0) {
      // forget the phone without touching the configuration; the device also takes a
      // new Bluetooth address at the reboot, so the phone pairs afresh without a Forget
      if (NimBLEDevice::isInitialized()) { obbStop(); NimBLEDevice::deleteAllBonds(); }
      cfg.renewBleAddress();
      Serial.println("[dbg] bonds deleted, new address at reboot");
      delay(200);
      ESP.restart();
    } else if (strcmp(line, "factory") == 0) {
      cfg.factoryReset();
      if (NimBLEDevice::isInitialized()) NimBLEDevice::deleteAllBonds();
      nvs_flash_deinit(); nvs_flash_erase();
      ESP.restart();
    } else if (strcmp(line, "log") == 0) {
      for (int i = 0; logGet(i); i++) {
        char stamp[16];
        logStamp(logGet(i), stamp, sizeof(stamp));
        Serial.printf("  %s %s\n", stamp, logGet(i)->text);
      }
    } else if (line[0]) {
      Serial.println("[dbg] commands: bg demo time status cfg set setup refresh warn alarm snooze ns dx llu sleep log btn rtc unbond mbforget poweroff reboot dfu factory");
    }
  }
}
