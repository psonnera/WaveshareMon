/*
  BleMiBand.cpp - Mi Band 2 emulation for xDrip+ (Android)
  (part of WaveshareMon, GPL v3, see LICENSE; ported from M5Stack_xDripMon)

  The protocol implemented here - service/characteristic UUIDs, the AES
  authentication flow, notification parsing and the Current Time layout -
  is derived from xDrip+ (Nightscout Foundation, GPL v3), specifically
  watch/miband/*.java (Const.java, OperationCodes.java, TimeMessage.java),
  which in turn documents the Huami protocol as reverse-engineered by the
  Gadgetbridge project. AES via mbedTLS (Apache-2.0, part of ESP-IDF).

  Copyright (C) 2023-2026 Patrick Sonnerat
*/
#include "BleMiBand.h"
#include "AppConfig.h"
#include "GlucoseState.h"
#include "TimeService.h"
#include "Battery.h"
#include "PowerCycle.h"
#include "Log.h"
#include <NimBLEDevice.h>
#include <mbedtls/aes.h>
#include <esp_random.h>

// Huami / Mi Band UUIDs (from xDrip watch/miband/Const.java)
#define UUID_SVC_MIBAND1     "FEE0"
#define UUID_SVC_MIBAND2     "FEE1"
#define UUID_SVC_ANS         "1811"
#define UUID_SVC_IMM_ALERT   "1802"
#define UUID_SVC_HEARTRATE   "180D"
#define UUID_SVC_DEVINFO     "180A"

#define UUID_CHR_AUTH        "00000009-0000-3512-2118-0009af100700"
#define UUID_CHR_BATTERY     "00000006-0000-3512-2118-0009af100700"
#define UUID_CHR_CONFIG      "00000003-0000-3512-2118-0009af100700"
#define UUID_CHR_DEVICEEVENT "00000010-0000-3512-2118-0009af100700"
#define UUID_CHR_CHUNKED     "00000020-0000-3512-2118-0009af100700"
#define UUID_CHR_USERSETT    "00000008-0000-3512-2118-0009af100700"
#define UUID_CHR_NEW_ALERT   "2A46"
#define UUID_CHR_ALERT_LEVEL "2A06"
#define UUID_CHR_ALERT_CTRL  "2A44"
#define UUID_CHR_HR_MEASURE  "2A37"
#define UUID_CHR_HR_CONTROL  "2A39"
#define UUID_CHR_SOFT_REV    "2A28"
#define UUID_CHR_SERIAL      "2A25"
#define UUID_CHR_HW_REV      "2A27"
#define UUID_CHR_CURR_TIME   "2A2B"

// auth opcodes (OperationCodes.java)
#define AUTH_SEND_KEY        0x01
#define AUTH_REQ_RANDOM      0x02
#define AUTH_SEND_ENCRYPTED  0x03
#define AUTH_RESPONSE        0x10
#define AUTH_SUCCESS         0x01
#define AUTH_FAIL            0x04

#define DEVICEEVENT_CALL_REJECT 0x07

static NimBLECharacteristic *chrBattery = nullptr;
static NimBLECharacteristic *chrDeviceEvent = nullptr;
static volatile bool s_enabled = false;
static volatile bool s_connected = false;
static volatile bool s_authOk = false;
static volatile uint16_t s_connHandle = 0xFFFF;
static volatile uint32_t s_readingMs = 0;     // millis() of the last BG write, 0 = none
static uint8_t  s_challenge[16];
static bool     s_challengeValid = false;
static void claimLink(NimBLEConnInfo &info);  // the link that talks Mi Band is xDrip's

// ---------------------------------------------------------------- battery

static void updateBatteryValue() {
  if (!chrBattery) return;
  uint8_t blob[20] = {0};
  int lvl = battery.percent();
  if (lvl < 0) lvl = 100;
  blob[0] = 0x0F;
  blob[1] = (uint8_t)lvl;
  blob[2] = battery.onUsb() ? 1 : 0;
  // plausible "last charge" record (xDrip only logs it)
  blob[10] = 25; blob[11] = 0; blob[12] = 1;
  blob[19] = 100;
  chrBattery->setValue(blob, sizeof(blob));
}

// ---------------------------------------------------------------- auth

class AuthCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *chr, NimBLEConnInfo &info) override {
    NimBLEAttValue v = chr->getValue();
    const uint8_t *d = v.data();
    size_t len = v.length();
    if (len < 2) return;
    claimLink(info);
    logDebug("miband auth op %02X", d[0]);

    if (d[0] == AUTH_SEND_KEY && len >= 18) {
      // xDrip hands us the 16-byte AES key
      memcpy(cfg.mibandKey, d + 2, 16);
      cfg.mibandKeySet = 1;
      cfg.save();
      uint8_t resp[3] = {AUTH_RESPONSE, AUTH_SEND_KEY, AUTH_SUCCESS};
      chr->setValue(resp, sizeof(resp));
      chr->notify();
      logAdd("xDrip: pairing key received");
    } else if (d[0] == AUTH_REQ_RANDOM) {
      esp_fill_random(s_challenge, sizeof(s_challenge));
      s_challengeValid = true;
      uint8_t resp[19];
      resp[0] = AUTH_RESPONSE;
      resp[1] = AUTH_REQ_RANDOM;
      resp[2] = AUTH_SUCCESS;
      memcpy(resp + 3, s_challenge, 16);
      chr->setValue(resp, sizeof(resp));
      chr->notify();
    } else if (d[0] == AUTH_SEND_ENCRYPTED && len >= 18) {
      uint8_t expected[16];
      bool ok = false;
      if (s_challengeValid && cfg.mibandKeySet) {
        mbedtls_aes_context aes;
        mbedtls_aes_init(&aes);
        mbedtls_aes_setkey_enc(&aes, cfg.mibandKey, 128);
        mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, s_challenge, expected);
        mbedtls_aes_free(&aes);
        ok = memcmp(expected, d + 2, 16) == 0;
      }
      uint8_t resp[3] = {AUTH_RESPONSE, AUTH_SEND_ENCRYPTED,
                         ok ? (uint8_t)AUTH_SUCCESS : (uint8_t)AUTH_FAIL};
      chr->setValue(resp, sizeof(resp));
      chr->notify();
      s_challengeValid = false;
      s_authOk = ok;
      if (ok) logDebug("miband auth ok");
      else    logAdd("xDrip auth FAILED (forget the MAC in xDrip, or mbforget)");
      gs.dataChanged = true;   // link icon
    }
  }
};

// ---------------------------------------------------------------- alerts

// parse "BG: 5,6 ↗" / "BG: 123 →" (uppercased by xDrip, any locale separator)
static bool parseBgText(const char *text, size_t len) {
  const char *p = nullptr;
  for (size_t i = 0; i + 3 <= len; i++) {
    if ((text[i] == 'B') && (text[i + 1] == 'G') && (text[i + 2] == ':')) {
      p = text + i + 3;
      break;
    }
  }
  if (!p) return false;
  while (*p == ' ') p++;

  char numBuf[12];
  size_t n = 0;
  while (n < sizeof(numBuf) - 1 && ((*p >= '0' && *p <= '9') || *p == '.' || *p == ',')) {
    numBuf[n++] = (*p == ',') ? '.' : *p;
    p++;
  }
  numBuf[n] = 0;
  if (n == 0) return false;                       // "HIGH" / "LOW": no number
  float val = atof(numBuf);
  uint16_t mgdl;
  if (strchr(numBuf, '.') || val < 30.0f)
    mgdl = (uint16_t)lroundf(val * 18.0f);        // mmol/L
  else
    mgdl = (uint16_t)lroundf(val);                // mg/dL

  while (*p == ' ') p++;
  int angle = slopeArrowToAngle(p);

  // the text carries no timestamp: a push follows the reading within seconds
  time_t now = time(nullptr);
  gs.onReading(mgdl, now > 1600000000 ? now : 0, angle);
  s_readingMs = millis();
  logDebug("miband bg %u", mgdl);
  return true;
}

class NewAlertCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *chr, NimBLEConnInfo &info) override {
    NimBLEAttValue v = chr->getValue();
    const uint8_t *d = v.data();
    size_t len = v.length();
    if (len < 3) return;
    claimLink(info);
    // band2:      [category][count][text...]
    // band3/4:    [0xFA][count][icon][0x00 text 0x00 title 0x00]
    // Extract printable/UTF-8 bytes; NULs and control bytes become spaces so
    // the "BG:" search below works for every packet layout.
    static char text[192];
    size_t n = 0;
    for (size_t i = 2; i < len && n < sizeof(text) - 1; i++) {
      char c = (char)d[i];
      text[n++] = (d[i] >= 32) ? c : ' ';
    }
    while (n && text[n - 1] == ' ') n--;
    text[n] = 0;
    if (!parseBgText(text, n) && n > 0) {
      gs.setInfoLine(text);                      // xDrip alert text on the bottom bar
      logAdd("xDrip: %.30s", text);
    }
  }
};

class AlertLevelCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *chr, NimBLEConnInfo &info) override {
    // vibration requests from xDrip alerts; local alarms already cover sound
    NimBLEAttValue v = chr->getValue();
    if (v.length() >= 1) logDebug("miband alert level %02X", v.data()[0]);
  }
};

class IgnoreCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *chr, NimBLEConnInfo &info) override {
    logDebug("miband write %s ignored", chr->getUUID().toString().c_str());
  }
};

class BatteryCallbacks : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic *chr, NimBLEConnInfo &info) override {
    updateBatteryValue();
  }
};

// Mi Band 2 current-time layout (xDrip TimeMessage / Gadgetbridge):
// [0-1] year LE, [2] month 1-12, [3] day, [4] hour, [5] minute, [6] second,
// [7] day of week, [8] fractions256, [9] adjust reason, [10] constant 0x0C.
// Bytes 0-6 carry the phone's local wall time; there is no usable timezone
// byte, so the configured offset applies. Current xDrip+ never writes this;
// the handler serves any client that does.
class CurrentTimeCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *chr, NimBLEConnInfo &info) override {
    NimBLEAttValue v = chr->getValue();
    const uint8_t *d = v.data();
    if (v.length() < 7) return;
    int year = d[0] | (d[1] << 8);
    int month = d[2], day = d[3], hour = d[4], minute = d[5], second = d[6];
    if (year < 2024 || year > 2099 || month < 1 || month > 12 ||
        day < 1 || day > 31 || hour > 23 || minute > 59 || second > 59) return;
    timeService.setManual(year, month, day, hour, minute, second);
    logAdd("time set from phone");
  }
  void onRead(NimBLECharacteristic *chr, NimBLEConnInfo &info) override {
    uint8_t b[11] = {0};
    struct tm t;
    if (timeService.getLocalTm(t)) {
      int year = t.tm_year + 1900;
      b[0] = (uint8_t)(year & 0xFF);
      b[1] = (uint8_t)(year >> 8);
      b[2] = (uint8_t)(t.tm_mon + 1);
      b[3] = (uint8_t)t.tm_mday;
      b[4] = (uint8_t)t.tm_hour;
      b[5] = (uint8_t)t.tm_min;
      b[6] = (uint8_t)t.tm_sec;
      b[7] = (uint8_t)(t.tm_wday == 0 ? 7 : t.tm_wday);   // 1=Mon .. 7=Sun
      b[9] = 0x01;
      b[10] = 0x0C;
    }
    chr->setValue(b, sizeof(b));
  }
};

// ---------------------------------------------------------------- server

// Connection bookkeeping is shared with the setup service (same server). The
// setup app connects to the same server, so a link only counts as xDrip's once
// it talks to the auth characteristic; these hooks come from BleSetupServer.
void miBandOnConnect(uint16_t connHandle) {
  if (!s_enabled) return;
  s_readingMs = 0;
}

void miBandOnDisconnect(uint16_t connHandle) {
  if (!s_enabled || connHandle != s_connHandle) return;
  s_connected = false;
  s_authOk = false;
  s_challengeValid = false;
  s_connHandle = 0xFFFF;
  gs.dataChanged = true;
}

static void claimLink(NimBLEConnInfo &info) {
  if (s_connHandle == info.getConnHandle()) return;
  s_connHandle = info.getConnHandle();
  s_connected = true;
  s_authOk = false;
  s_challengeValid = false;
  gs.dataChanged = true;
}

void miBandBegin() {
  NimBLEServer *server = NimBLEDevice::getServer();
  if (!server) return;
  s_enabled = true;
  s_authOk = false;

  // device information
  NimBLEService *devInfo = server->createService(UUID_SVC_DEVINFO);
  devInfo->createCharacteristic(UUID_CHR_SOFT_REV, NIMBLE_PROPERTY::READ)->setValue("1.0.1.81");
  devInfo->createCharacteristic(UUID_CHR_HW_REV, NIMBLE_PROPERTY::READ)->setValue("V0.8.3.4");
  devInfo->createCharacteristic(UUID_CHR_SERIAL, NIMBLE_PROPERTY::READ)->setValue("WaveshareMon");

  // main Huami service
  NimBLEService *fee0 = server->createService(UUID_SVC_MIBAND1);
  chrBattery = fee0->createCharacteristic(UUID_CHR_BATTERY,
                 NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  chrBattery->setCallbacks(new BatteryCallbacks());
  updateBatteryValue();
  fee0->createCharacteristic(UUID_CHR_CONFIG,
                 NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::NOTIFY)
      ->setCallbacks(new IgnoreCallbacks());
  chrDeviceEvent = fee0->createCharacteristic(UUID_CHR_DEVICEEVENT, NIMBLE_PROPERTY::NOTIFY);
  fee0->createCharacteristic(UUID_CHR_CHUNKED, NIMBLE_PROPERTY::WRITE_NR)
      ->setCallbacks(new IgnoreCallbacks());
  fee0->createCharacteristic(UUID_CHR_USERSETT,
                 NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR)
      ->setCallbacks(new IgnoreCallbacks());
  fee0->createCharacteristic(UUID_CHR_CURR_TIME,
                 NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE)
      ->setCallbacks(new CurrentTimeCallbacks());

  // auth service
  NimBLEService *fee1 = server->createService(UUID_SVC_MIBAND2);
  fee1->createCharacteristic(UUID_CHR_AUTH,
                 NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::NOTIFY)
      ->setCallbacks(new AuthCallbacks());

  // alert notification service (BG arrives here)
  NimBLEService *ans = server->createService(UUID_SVC_ANS);
  ans->createCharacteristic(UUID_CHR_NEW_ALERT,
                 NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR)
      ->setCallbacks(new NewAlertCallbacks());
  ans->createCharacteristic(UUID_CHR_ALERT_CTRL,
                 NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR)
      ->setCallbacks(new IgnoreCallbacks());

  // immediate alert (vibration)
  NimBLEService *imm = server->createService(UUID_SVC_IMM_ALERT);
  imm->createCharacteristic(UUID_CHR_ALERT_LEVEL,
                 NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR)
      ->setCallbacks(new AlertLevelCallbacks());

  // heart rate (xDrip may subscribe / write the control point)
  NimBLEService *hr = server->createService(UUID_SVC_HEARTRATE);
  hr->createCharacteristic(UUID_CHR_HR_MEASURE, NIMBLE_PROPERTY::NOTIFY);
  hr->createCharacteristic(UUID_CHR_HR_CONTROL,
                 NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::READ)
      ->setCallbacks(new IgnoreCallbacks());

  devInfo->start();
  fee0->start();
  fee1->start();
  ans->start();
  imm->start();
  hr->start();

  // advertising data (FEE0 + name "MI Band 2") is built by setupServerBegin()
  NimBLEDevice::startAdvertising();
  logAdd("xDrip: waiting as MI Band 2");
}

void miBandTick() {
  if (!s_enabled) return;
  // keep the battery characteristic fresh once a minute
  static uint32_t lastBatt = 0;
  if (millis() - lastBatt > 60000) {
    lastBatt = millis();
    updateBatteryValue();
  }
  // xDrip has delivered its reading: release the link so the cycle can end.
  // Not while awake: the phone shares one link between xDrip and the setup
  // app, and xDrip closes its side by itself once it is done.
  if (s_connected && s_readingMs && millis() - s_readingMs > 1500 && !cycleAwake()) {
    s_readingMs = 0;
    NimBLEServer *server = NimBLEDevice::getServer();
    if (server && s_connHandle != 0xFFFF) server->disconnect(s_connHandle);
  }
}

void miBandStop() {
  if (!s_enabled) return;
  NimBLEServer *server = NimBLEDevice::getServer();
  if (server && s_connected && s_connHandle != 0xFFFF) {
    server->disconnect(s_connHandle);
    delay(50);
  }
}

bool miBandConnected() { return s_enabled && s_connected; }
bool miBandIsAuthenticated() { return s_enabled && s_connected && s_authOk; }

const char *miBandStateName() {
  if (!s_enabled) return "off";
  if (!s_connected) return cfg.mibandKeySet ? "waiting" : "not paired";
  return s_authOk ? "connected" : "auth";
}

void miBandSendSnooze() {
  if (!s_connected || !chrDeviceEvent) return;
  uint8_t ev = DEVICEEVENT_CALL_REJECT;
  chrDeviceEvent->setValue(&ev, 1);
  chrDeviceEvent->notify();
  logDebug("miband snooze sent");
}

void miBandForgetKey() {
  memset(cfg.mibandKey, 0, sizeof(cfg.mibandKey));
  cfg.mibandKeySet = 0;
  cfg.save();
  logAdd("Mi Band key forgotten");
}
