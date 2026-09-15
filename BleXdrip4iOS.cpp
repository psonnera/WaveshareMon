/*
  BleXdrip4iOS.cpp - "M5Stack" protocol of xDrip4iOS / xdripswift
  (part of WaveshareMon, GPL v3, see LICENSE)

  Ported from M5Stack_xDripMon, itself a port of the xDrip4iOS fork of
  M5_NightscoutMon by Johan Degraeve (github.com/JohanDegraeve/M5_NightscoutMon,
  GPL v3), based on M5_NightscoutMon, Copyright (C) Martin Lukasek. Opcodes and
  framing match the app side in xdripswift / xDrip4iOS, Copyright (C) Johan
  Degraeve, GPL v3.

  Copyright (C) 2023-2026 Patrick Sonnerat
*/
#include "BleXdrip4iOS.h"
#include "AppConfig.h"
#include "GlucoseState.h"
#include "TimeService.h"
#include "Battery.h"
#include "EpdUi.h"
#include "Log.h"
#include <NimBLEDevice.h>
#include <esp_random.h>

void powerOff();                          // WaveshareMon.ino

#define SERVICE_UUID  "AF6E5F78-706A-43FB-B1F4-C27D7D5C762F"
#define CHAR_UUID     "6D810E9F-0983-4030-BDA7-C7C9A6A19C1C"
#define MAX_PKT       20

// while the device stays awake (setup mode, USB bench): data stale after about
// one missed reading -> nudge the app; two missed readings -> rebuild the link
#define HEARTBEAT_STALE_MS     (6UL * 60 * 1000)
#define HEARTBEAT_THROTTLE_MS  (60UL * 1000)
#define FORCE_RECONNECT_MS     (11UL * 60 * 1000)

extern Battery battery;
extern EpdUi ui;

static NimBLECharacteristic *s_chr = nullptr;
static bool     s_enabled = false;
static volatile bool s_connected = false;
static volatile uint16_t s_connHandle = 0xFFFF;
static bool     s_authOk = false;
static bool     s_paramsRequested = false;   // 0x16 sent once per connection
static uint32_t s_lastTimeReqMs = 0;
static uint32_t s_lastActivityMs = 0;
static uint32_t s_lastHeartbeatMs = 0;
static uint32_t s_readingMs = 0;
static char     s_pendingDir[24] = "";
// time: the app sends the local epoch (0x12) and the offset (0x14) as two frames
static time_t   s_localEpoch = 0;
static uint32_t s_localEpochMs = 0;
static int32_t  s_tzOffset = 0;
static bool     s_tzKnown = false;
static char     s_name[32] = "";

const char *xdrip4iosName() {
  // the app accepts any name containing "M5Stack"; keep ours recognisable
  // and inside the 29 bytes a scan response allows
  snprintf(s_name, sizeof(s_name), "M5Stack %.21s", cfg.name());
  return s_name;
}

// frame: [opcode][packetNo 1-based][totalPackets][ascii payload], <= 20 bytes
static void sendToClient(const char *text, uint8_t opCode) {
  if (!s_chr || !s_connected) return;
  int size = text ? strlen(text) : 0;
  if (size == 0) {
    uint8_t pkt[3] = {opCode, 0x01, 0x01};
    s_chr->setValue(pkt, 3);
    s_chr->notify();
    return;
  }
  int total = (size + (MAX_PKT - 3) - 1) / (MAX_PKT - 3);
  int sent = 0, num = 1;
  while (sent < size) {
    int chunk = size - sent;
    if (chunk > MAX_PKT - 3) chunk = MAX_PKT - 3;
    uint8_t pkt[MAX_PKT];
    pkt[0] = opCode;
    pkt[1] = (uint8_t)num;
    pkt[2] = (uint8_t)total;
    memcpy(pkt + 3, text + sent, chunk);
    s_chr->setValue(pkt, chunk + 3);
    s_chr->notify();
    sent += chunk;
    num++;
  }
}

static void generatePassword() {
  static const char letters[] = "abcdefghijklmnopqrstuvwxyz0123456789";
  for (int i = 0; i < 10; i++) cfg.x4iPassword[i] = letters[esp_random() % 36];
  cfg.x4iPassword[10] = 0;
  cfg.save();
}

static void applyTime() {
  if (!s_localEpoch || !s_tzKnown) return;
  time_t utc = s_localEpoch - s_tzOffset + (time_t)((millis() - s_localEpochMs) / 1000);
  timeService.setFromUtc(utc, s_tzOffset);
}

class CharCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &) override {
    NimBLEAttValue v = c->getValue();
    const uint8_t *d = v.data();
    size_t len = v.length();
    if (len == 0) return;
    uint8_t op = d[0];
    char payload[128] = "";
    if (len > 3) {
      size_t n = len - 3;
      if (n > sizeof(payload) - 1) n = sizeof(payload) - 1;
      memcpy(payload, d + 3, n);
      payload[n] = 0;
    }
    logDebug("x4i rx %02X len %u", op, (unsigned)len);

    switch (op) {
      // settings the app carries for a real M5Stack: keep them, they make the
      // Nightscout source and the firmware update usable without typing
      case 0x01: strlcpy(cfg.nsUrl, payload, sizeof(cfg.nsUrl)); cfg.save(); break;
      case 0x02: strlcpy(cfg.nsToken, payload, sizeof(cfg.nsToken)); cfg.save(); break;
      case 0x07: strlcpy(cfg.wifiSsid, payload, sizeof(cfg.wifiSsid)); cfg.save(); break;
      case 0x08: strlcpy(cfg.wifiPass, payload, sizeof(cfg.wifiPass)); cfg.save(); break;

      case 0x03: {                          // units: "true" = mg/dL
        uint8_t units = strcmp(payload, "true") == 0 ? UNITS_MGDL : UNITS_MMOL;
        if (units != cfg.units) { cfg.units = units; cfg.save(); ui.requestRedraw(); }
      } break;

      case 0x09:                            // the app asks for the password
        if (cfg.x4iPassword[0] == 0) {
          generatePassword();
          sendToClient(cfg.x4iPassword, 0x0E);
          s_authOk = true;
          logAdd("xDrip4iOS: paired");
        } else {
          sendToClient("", 0x0F);           // one exists: authenticate, or reset it here
        }
        gs.dataChanged = true;
        break;

      case 0x0A: {                          // authenticate: [0x0A][password]
        if (cfg.x4iPassword[0] == 0) {
          generatePassword();
          sendToClient(cfg.x4iPassword, 0x0E);
          s_authOk = true;
          logAdd("xDrip4iOS: paired");
        } else {
          size_t pw = strlen(cfg.x4iPassword);
          bool ok = len == pw + 1 && memcmp(d + 1, cfg.x4iPassword, pw) == 0;
          sendToClient("", ok ? 0x0B : 0x0C);
          s_authOk = ok;
          if (ok) logDebug("x4i auth ok");
          else    logAdd("xDrip4iOS auth FAILED (reset the password)");
        }
        gs.dataChanged = true;
      } break;

      case 0x10: {                          // "mgdl epochSecondsUTC"
        if (!s_authOk) break;
        char *sp = strchr(payload, ' ');
        if (!sp) break;
        *sp = 0;
        int mgdl = atoi(payload);
        time_t utc = (time_t)strtoul(sp + 1, nullptr, 10);
        if (mgdl <= 0) break;
        gs.onReading((uint16_t)mgdl, utc > 1600000000 ? utc : 0, nsDirectionToAngle(s_pendingDir));
        s_readingMs = millis();
        s_lastActivityMs = s_readingMs;
        logDebug("x4i bg %d", mgdl);
      } break;

      case 0x13: {                          // trend as a Nightscout direction name
        if (!s_authOk) break;
        strlcpy(s_pendingDir, payload, sizeof(s_pendingDir));
        // the value usually arrives first: fix its arrow up
        if (s_readingMs && millis() - s_readingMs < 10000) {
          int a = nsDirectionToAngle(s_pendingDir);
          if (a != gs.arrowAngle) { gs.arrowAngle = a; gs.dataChanged = true; }
        }
      } break;

      case 0x12: {                          // local time, epoch seconds
        if (!s_authOk) break;
        unsigned long local = strtoul(payload, nullptr, 10);
        if (local > 1600000000UL) {
          s_localEpoch = (time_t)local;
          s_localEpochMs = millis();
          if (!s_tzKnown && !timeService.known()) {
            // no offset yet: the configured one is the best guess
            s_tzOffset = cfg.tzOffsetSec; s_tzKnown = true;
            applyTime();
            s_tzKnown = false;
          } else applyTime();
        }
        if (!s_paramsRequested) { s_paramsRequested = true; sendToClient("", 0x16); }
      } break;

      case 0x14: {                          // time zone offset, seconds (local - UTC)
        if (!s_authOk) break;
        s_tzOffset = (int32_t)strtol(payload, nullptr, 10);
        s_tzKnown = true;
        applyTime();
      } break;

      case 0x15: case 0x17: case 0x18: case 0x19:   // colours, rotation, brightness: no such things here
        break;

      case 0x21: {                          // battery level request -> 0x20
        if (!s_authOk) break;
        int pct = battery.percent();
        uint8_t pkt[2] = {0x20, (uint8_t)(pct < 0 ? 100 : pct)};
        s_chr->setValue(pkt, 2);
        s_chr->notify();
      } break;

      case 0x22:                            // power off from the app
        if (s_authOk) powerOff();
        break;

      default:
        break;
    }
  }
} s_charCb;

void xdrip4iosBegin() {
  NimBLEServer *server = NimBLEDevice::getServer();
  if (!server) return;
  s_enabled = true;
  NimBLEService *svc = server->createService(SERVICE_UUID);
  s_chr = svc->createCharacteristic(CHAR_UUID,
            NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
  s_chr->setCallbacks(&s_charCb);
  svc->start();
  logAdd("xDrip4iOS: waiting as %s", xdrip4iosName());
}

void xdrip4iosOnConnect(uint16_t connHandle) {
  if (!s_enabled) return;
  s_connHandle = connHandle;
  s_connected = true;
  s_authOk = false;
  s_paramsRequested = false;
  s_lastActivityMs = millis();
  s_lastHeartbeatMs = 0;
  gs.dataChanged = true;
}

void xdrip4iosOnDisconnect(uint16_t connHandle) {
  if (!s_enabled || connHandle != s_connHandle) return;
  s_connected = false;
  s_authOk = false;
  s_connHandle = 0xFFFF;
  gs.dataChanged = true;
}

void xdrip4iosTick() {
  if (!s_enabled || !s_connected || !s_authOk) return;
  uint32_t now = millis();
  // ask the app for the time while we do not know it
  if (!timeService.known() && now - s_lastTimeReqMs > 30000UL) {
    s_lastTimeReqMs = now;
    sendToClient("", 0x11);
  }
  // an iPhone can drift out of range without a clean disconnect
  uint32_t stale = now - s_lastActivityMs;
  if (stale > HEARTBEAT_STALE_MS && now - s_lastHeartbeatMs > HEARTBEAT_THROTTLE_MS) {
    s_lastHeartbeatMs = now;
    sendToClient("", 0x21);
    logDebug("x4i heartbeat");
  }
  if (stale > FORCE_RECONNECT_MS) {
    NimBLEServer *server = NimBLEDevice::getServer();
    if (server) { logAdd("xDrip4iOS: stale link, reconnecting"); server->disconnect(s_connHandle); }
    s_lastActivityMs = now;
  }
}

void xdrip4iosStop() {
  if (!s_enabled) return;
  NimBLEServer *server = NimBLEDevice::getServer();
  if (server && s_connected && s_connHandle != 0xFFFF) {
    server->disconnect(s_connHandle);
    delay(50);
  }
}

bool xdrip4iosConnected() { return s_enabled && s_connected; }
bool xdrip4iosIsAuthenticated() { return s_enabled && s_connected && s_authOk; }

const char *xdrip4iosStateName() {
  if (!s_enabled) return "off";
  if (!s_connected) return cfg.x4iPassword[0] ? "waiting" : "not paired";
  return s_authOk ? "connected" : "auth";
}

void xdrip4iosForgetPassword() {
  cfg.x4iPassword[0] = 0;
  cfg.save();
  s_authOk = false;
  logAdd("xDrip4iOS password reset");
  gs.dataChanged = true;
}
