/*
  BleObbClient.cpp - xDrip Open Bluetooth Broadcast receiver (BLE central)
  (part of WaveshareMon, GPL v3, see LICENSE)

  Copyright (C) 2026 Patrick Sonnerat
*/
#include "BleObbClient.h"
#include "AppConfig.h"
#include "GlucoseState.h"
#include "Alarms.h"
#include "TimeService.h"
#include "Log.h"
#include <NimBLEDevice.h>

// OBB service and characteristic UUIDs (spec 3.1, frozen from v0.1)
static const NimBLEUUID UUID_SVC   ("e9ca0001-e28a-47ac-bebb-2f51794c9581");
static const NimBLEUUID UUID_GLU   ("e9ca0002-e28a-47ac-bebb-2f51794c9581");
static const NimBLEUUID UUID_ALARM ("e9ca0003-e28a-47ac-bebb-2f51794c9581");
static const NimBLEUUID UUID_STATUS("e9ca0004-e28a-47ac-bebb-2f51794c9581");
static const NimBLEUUID UUID_SLINE ("e9ca0006-e28a-47ac-bebb-2f51794c9581");

#pragma pack(push, 1)
typedef struct {
  uint8_t  version;
  uint8_t  flags;      // bit0 stale, bit1 warmup, bit2 raw
  uint16_t age;        // seconds since reading, 0xFFFF = unknown
  uint16_t glucose;    // mg/dl x10, 0xFFFF = unavailable
  int16_t  delta;      // mg/dl x10, 0x7FFF = unavailable
  uint8_t  trend;
  uint8_t  reserved;
} obb_glucose_t;
typedef struct {
  uint8_t  version;
  uint8_t  alarm_type;
  uint16_t age;
  uint16_t value;
  uint8_t  reserved;
} obb_alarm_t;
typedef struct {
  uint8_t  protocol_version;
  uint8_t  capabilities;
  uint8_t  phone_battery;
  uint8_t  source;
  uint32_t utc_time;
  int8_t   tz_offset;  // 15-minute units
} obb_status_t;
#pragma pack(pop)

volatile bool obbStateChanged = false;

static ObbState       s_state = OBB_IDLE;
static bool           s_enabled = false;
static bool           s_paused = false;
static NimBLEClient  *s_client = nullptr;
static NimBLEAddress  s_target;
static volatile bool  s_found = false;
static volatile bool  s_disconnected = false;
static uint32_t       s_nextActionMs = 0;
static uint32_t       s_lastPacketMs = 0;
static uint32_t       s_connectedMs = 0;

#define SUPERVISION_MS   (16UL * 60 * 1000)   // no packet for 16 min: reconnect
#define RETRY_MS         (5UL * 1000)
#define PAIR_FAIL_MS     (30UL * 1000)

static void setState(ObbState s) {
  if (s_state == s) return;
  s_state = s;
  obbStateChanged = true;
}

// ---- callbacks (NimBLE host task) ---------------------------------------------

class ScanCb : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *dev) override {
    if (s_found || !dev->isAdvertisingService(UUID_SVC)) return;
    s_target = dev->getAddress();
    s_found = true;
    NimBLEDevice::getScan()->stop();
  }
  void onScanEnd(const NimBLEScanResults &, int) override {}
} s_scanCb;

class ClientCb : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient *) override {}
  void onDisconnect(NimBLEClient *, int reason) override {
    logAdd("xDrip disconnected (%d)", reason);
    s_disconnected = true;
  }
  void onAuthenticationComplete(NimBLEConnInfo &info) override {
    if (!info.isEncrypted()) logAdd("BLE encryption failed");
    else if (info.isBonded()) logDebug("bonded");
  }
} s_clientCb;

static void onGlucose(NimBLERemoteCharacteristic *, uint8_t *data, size_t len, bool) {
  if (len < sizeof(obb_glucose_t)) return;
  obb_glucose_t r; memcpy(&r, data, sizeof(r));
  s_lastPacketMs = millis();
  if (r.glucose == 0xFFFF) return;
  time_t now = time(nullptr);
  time_t utc = 0;
  if (now > 1600000000 && r.age != 0xFFFF) utc = now - r.age;
  uint16_t mgdl = (r.glucose + 5) / 10;
  gs.onReading(mgdl, utc, obbTrendToAngle(r.trend));
  if (r.delta != 0x7FFF) gs.setDelta((int16_t)((r.delta + (r.delta >= 0 ? 5 : -5)) / 10), true);
  gs.remoteStale = (r.flags & 0x01) != 0;
  logDebug("obb bg %u age %u trend %u fl %02X", mgdl, r.age, r.trend, r.flags);
}

static void onAlarm(NimBLERemoteCharacteristic *, uint8_t *data, size_t len, bool) {
  if (len < sizeof(obb_alarm_t)) return;
  obb_alarm_t a; memcpy(&a, data, sizeof(a));
  s_lastPacketMs = millis();
  alarms.onRemoteAlarm(a.alarm_type, a.value == 0xFFFF ? 0 : (a.value + 5) / 10);
}

static void onStatusLine(NimBLERemoteCharacteristic *chr, uint8_t *data, size_t len, bool) {
  // the notification only carries the beginning; do a (long) read for the full text
  NimBLEAttValue v = chr->readValue();
  std::string s = v.length() ? std::string(v.c_str(), v.length()) : std::string((const char *)data, len);
  for (auto &c : s) if (c == '\n' || c == '\r') c = ' ';
  gs.setInfoLine(s.c_str());
}

// ---- connection sequence (main loop context) ----------------------------------

static void startScan() {
  s_found = false;
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&s_scanCb, false);
  scan->setActiveScan(true);
  scan->setInterval(160);
  scan->setWindow(80);
  scan->start(0, false, true);
  setState(OBB_SCANNING);
}

static bool connectAndSubscribe() {
  setState(OBB_CONNECTING);
  logAdd("xDrip found %s", s_target.toString().c_str());
  if (!s_client) {
    s_client = NimBLEDevice::createClient();
    s_client->setClientCallbacks(&s_clientCb, false);
    s_client->setConnectionParams(24, 40, 4, 400);   // relaxed: data every 5 min
    s_client->setConnectTimeout(10000);
  }
  if (!s_client->connect(s_target, true)) {
    logAdd("connect failed");
    return false;
  }
  // encrypted link is mandatory; first contact bonds (Just Works) while the
  // user has the pairing window open in xDrip
  if (!s_client->secureConnection()) {
    logAdd("pairing refused - open pairing mode in xDrip");
    s_client->disconnect();
    // a stale bond (phone forgot us) would make every later attempt fail:
    // drop it so the next connection pairs afresh
    NimBLEDevice::deleteBond(s_target);
    s_nextActionMs = millis() + PAIR_FAIL_MS;
    return false;
  }
  NimBLERemoteService *svc = s_client->getService(UUID_SVC);
  if (!svc) { logAdd("OBB service missing"); s_client->disconnect(); return false; }

  NimBLERemoteCharacteristic *st = svc->getCharacteristic(UUID_STATUS);
  if (st && st->canRead()) {
    NimBLEAttValue v = st->readValue();
    if (v.length() >= sizeof(obb_status_t)) {
      obb_status_t s; memcpy(&s, v.data(), sizeof(s));
      if (s.utc_time > 1600000000UL)
        timeService.setFromUtc((time_t)s.utc_time, (int32_t)s.tz_offset * 900);
      logDebug("obb v%u caps %02X bat %u src %u", s.protocol_version, s.capabilities,
               s.phone_battery, s.source);
    }
  }
  NimBLERemoteCharacteristic *glu = svc->getCharacteristic(UUID_GLU);
  if (!glu || !glu->subscribe(true, onGlucose)) {
    logAdd("glucose subscribe failed");
    s_client->disconnect();
    return false;
  }
  NimBLERemoteCharacteristic *al = svc->getCharacteristic(UUID_ALARM);
  if (al) al->subscribe(true, onAlarm);
  if (cfg.obbStatusLine) {
    NimBLERemoteCharacteristic *sl = svc->getCharacteristic(UUID_SLINE);
    if (sl && sl->subscribe(true, onStatusLine)) {
      NimBLEAttValue v = sl->readValue();
      if (v.length()) gs.setInfoLine(v.c_str());
    }
  }
  s_lastPacketMs = millis();
  s_connectedMs = millis();
  s_disconnected = false;
  setState(OBB_CONNECTED);
  logAdd("xDrip connected");
  return true;
}

// ---- public -------------------------------------------------------------------

void obbBegin() {
  s_enabled = true;
  s_nextActionMs = millis() + 500;
  setState(OBB_IDLE);
}

void obbStop() {
  s_enabled = false;
  NimBLEDevice::getScan()->stop();
  if (s_client && s_client->isConnected()) s_client->disconnect();
  setState(OBB_IDLE);
}

void obbSetPaused(bool paused) {
  if (s_paused == paused) return;
  s_paused = paused;
  if (paused) {
    NimBLEDevice::getScan()->stop();
    if (s_state == OBB_SCANNING) setState(OBB_IDLE);
  } else {
    s_nextActionMs = millis() + 1000;
  }
}

ObbState obbState() { return s_state; }

const char *obbStateName() {
  switch (s_state) {
    case OBB_SCANNING:   return "scanning";
    case OBB_CONNECTING: return "connecting";
    case OBB_CONNECTED:  return "connected";
    default:             return "idle";
  }
}

void obbTick() {
  if (!s_enabled || s_paused) return;
  uint32_t now = millis();

  if (s_state == OBB_CONNECTED) {
    if (s_disconnected || !s_client->isConnected()) {
      s_disconnected = false;
      setState(OBB_IDLE);
      s_nextActionMs = now + RETRY_MS;
    } else if (now - s_lastPacketMs > SUPERVISION_MS) {
      logAdd("no OBB data for 16 min, reconnecting");
      s_client->disconnect();
      s_nextActionMs = now + RETRY_MS;
    }
    return;
  }

  if (s_found) {
    s_found = false;
    if (!connectAndSubscribe()) {
      setState(OBB_IDLE);
      if ((int32_t)(s_nextActionMs - now) < (int32_t)RETRY_MS) s_nextActionMs = now + RETRY_MS;
    }
    return;
  }

  if (s_state == OBB_IDLE && (int32_t)(now - s_nextActionMs) >= 0) startScan();
}
