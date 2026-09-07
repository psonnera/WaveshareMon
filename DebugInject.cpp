/*
  DebugInject.cpp - serial test commands
  (part of WaveshareMon, GPL v3, see LICENSE; derived from M5Stack_xDripMon)

  Copyright (C) 2026 Patrick Sonnerat
*/
#include "DebugInject.h"
#include "GlucoseState.h"
#include "TimeService.h"
#include "AppConfig.h"
#include "Alarms.h"
#include "Battery.h"
#include "BleObbClient.h"
#include "BleSetupServer.h"
#include "WifiService.h"
#include "NightscoutClient.h"
#include "EpdUi.h"
#include "Log.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <nvs_flash.h>

static void printCfg() {
  Serial.printf("[cfg] name=%s src=%u units=%u ssid=%s pass=%s url=%s token=%s tz=%s/%ld\n",
                cfg.name(), cfg.source, cfg.units, cfg.wifiSsid, cfg.wifiPass[0] ? "***" : "",
                cfg.nsUrl, cfg.nsToken[0] ? "***" : "", cfg.tzString, (long)cfg.tzOffsetSec);
  Serial.printf("[cfg] colours y%u-%u r%u-%u alarms en=%u w%u-%u a%u-%u noread=%u vol %u/%u rep=%u snooze=%u sline=%u\n",
                cfg.yellowLow, cfg.yellowHigh, cfg.redLow, cfg.redHigh, cfg.alarmsEnabled,
                cfg.warnLow, cfg.warnHigh, cfg.alarmLow, cfg.alarmHigh, cfg.noReadingsMin,
                cfg.warnVolume, cfg.alarmVolume, cfg.alarmRepeatMin, cfg.snoozeMin, cfg.obbStatusLine);
}

static bool setKey(const char *key, const char *val) {
  long n = atol(val);
  if      (!strcmp(key, "src"))   cfg.source = n ? SRC_NIGHTSCOUT : SRC_OBB;
  else if (!strcmp(key, "units")) cfg.units = n ? UNITS_MMOL : UNITS_MGDL;
  else if (!strcmp(key, "ssid"))  strlcpy(cfg.wifiSsid, val, sizeof(cfg.wifiSsid));
  else if (!strcmp(key, "pass"))  strlcpy(cfg.wifiPass, val, sizeof(cfg.wifiPass));
  else if (!strcmp(key, "url"))   strlcpy(cfg.nsUrl, val, sizeof(cfg.nsUrl));
  else if (!strcmp(key, "token")) strlcpy(cfg.nsToken, val, sizeof(cfg.nsToken));
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
  else return false;
  cfg.save();
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
      setupServerAdvertise(on);
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
    } else if (strcmp(line, "reboot") == 0) {
      ESP.restart();
    } else if (strcmp(line, "factory") == 0) {
      cfg.factoryReset();
      NimBLEDevice::deleteAllBonds();
      nvs_flash_deinit(); nvs_flash_erase();
      ESP.restart();
    } else if (strcmp(line, "log") == 0) {
      for (int i = 0; logGet(i); i++) Serial.printf("  %s\n", logGet(i)->text);
    } else if (line[0]) {
      Serial.println("[dbg] commands: bg demo time status cfg set setup refresh warn alarm snooze ns log reboot factory");
    }
  }
}
