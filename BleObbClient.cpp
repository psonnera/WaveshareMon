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
#include "BleBonds.h"
#include "BleSetupServer.h"
#include "PowerCycle.h"
#include "EpdUi.h"
#include <NimBLEDevice.h>
#include <esp_task_wdt.h>
#include "nimble/nimble/host/include/host/ble_store.h"

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
static uint8_t        s_secureFails = 0;       // consecutive secureConnection() failures on a bonded link

#define SUPERVISION_MS   (16UL * 60 * 1000)   // no packet for 16 min: reconnect
#define RETRY_MS         (5UL * 1000)
#define PAIR_FAIL_MS     (30UL * 1000)
#define SECURE_FAIL_LIMIT 4                    // drop a bond only after this many straight failures
#define SETUP_RELINK_MS  (2UL * 60 * 1000)     // setup mode: the link is released after a reading, back in 2 min
static uint32_t       s_readingAtMs = 0;       // millis() of the reading received on this link (0: none yet)

static void setState(ObbState s) {
  if (s_state == s) return;
  s_state = s;
  obbStateChanged = true;
}

// ---- callbacks (NimBLE host task) ---------------------------------------------

class ScanCb : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *dev) override {
    if (s_found || !dev->isAdvertisingService(UUID_SVC)) return;
    // The phone advertises with a resolvable private address; the controller
    // resolves it against the restored IRK and reports the identity as an
    // *_ID type (PUBLIC_ID/RANDOM_ID). Connecting with that ID type makes the
    // link record the on-air RPA as the peer, so the stored bond (keyed on the
    // plain identity address) is not found and the board re-pairs on every
    // reconnect. Normalise the type to the base identity type so the security
    // lookup matches the bond and the link is simply re-encrypted.
    ble_addr_t a = *dev->getAddress().getBase();
    if (a.type == BLE_ADDR_PUBLIC_ID)      a.type = BLE_ADDR_PUBLIC;
    else if (a.type == BLE_ADDR_RANDOM_ID) a.type = BLE_ADDR_RANDOM;
    s_target = NimBLEAddress(a);
    s_found = true;
    NimBLEDevice::getScan()->stop();
  }
  void onScanEnd(const NimBLEScanResults &, int) override {}
} s_scanCb;

class ClientCb : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient *) override {}
  uint32_t onPassKeyDisplay(NimBLEConnInfo &) override {
    uint32_t k = setupPasskey();
    ui.showPasskey(k);                      // drawn by the main loop
    logDebug("passkey %06lu", (unsigned long)k);
    return k;
  }
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
  s_readingAtMs = s_lastPacketMs;
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

static NimBLERemoteCharacteristic *s_statusChr = nullptr;   // valid while connected (the attribute cache)
static volatile bool s_statusDirty = false;                  // a status-line notification waits for its full read

static void setInfoLine(const char *data, size_t len) {
  std::string s(data, len);
  for (auto &c : s) if (c == '\n' || c == '\r') c = ' ';
  gs.setInfoLine(s.c_str());
}

static void onStatusLine(NimBLERemoteCharacteristic *chr, uint8_t *data, size_t len, bool) {
  // Host task: no GATT call here. A blocking read would wait for the answer that
  // this very task has to process, and the host stops for good: the link then
  // answers nothing, its disconnect never completes and the setup service is
  // dead until a reboot (found 2026-09-20). The notification only carries the
  // beginning of the text: show it now, the main loop reads the full text (obbTick).
  setInfoLine((const char *)data, len);
  s_statusChr = chr;
  s_statusDirty = true;
}

// ---- connection sequence (main loop context) ----------------------------------

static void logBonds();

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
  s_readingAtMs = 0;                      // the subscriptions below already deliver the first reading
  s_statusDirty = false;
  s_statusChr = nullptr;                  // connect() rebuilds the attribute cache
  logAdd("xDrip found %s/t%d", s_target.toString().c_str(), s_target.getType());
  if (!s_client) {
    s_client = NimBLEDevice::createClient();
    s_client->setClientCallbacks(&s_clientCb, false);
    // default connection parameters during pairing; relaxed ones are requested
    // once the link is encrypted (see below)
    s_client->setConnectTimeout(10000);
  }
  // The ESP32-S3 controller refuses to initiate a connection while it is
  // advertising (LE Create Connection fails with BLE_ERR_MEM_CAPACITY, rc 519),
  // so the setup advertising is paused for the duration of the attempt.
  bool wasAdvertising = NimBLEDevice::getAdvertising()->isAdvertising();
  if (wasAdvertising) NimBLEDevice::stopAdvertising();
  // No MTU exchange inside connect(): it must return the moment the link is
  // up so that encryption can be started first (see below).
  bool connected = s_client->connect(s_target, true, false, false);
  if (wasAdvertising) NimBLEDevice::startAdvertising();
  if (!connected) {
    // typical cause: xDrip drops unbonded links while its pairing window is closed
    logAdd("connect failed (rc %d) - pairing mode open?", s_client->getLastError());
    s_nextActionMs = millis() + PAIR_FAIL_MS;
    return false;
  }
  // Which peer identity the link carries and whether the host has its key. No
  // key means a pairing follows (first contact, or the phone's identity key
  // changed so its private address did not resolve to the bonded identity).
  bool hadKey;
  {
    NimBLEConnInfo ci = s_client->getConnInfo();
    NimBLEAddress id = ci.getIdAddress();
    struct ble_store_key_sec k = {};
    k.peer_addr = *id.getBase();
    struct ble_store_value_sec v = {};
    int rc = ble_store_read_peer_sec(&k, &v);
    hadKey = rc == 0 && v.ltk_present;
    logDebug("id %s/t%d key rc=%d ltk=%d auth=%d", id.toString().c_str(), id.getType(), rc,
             rc == 0 ? v.ltk_present : -1, rc == 0 ? v.authenticated : -1);
    if (hadKey && !v.authenticated) {
      // Just Works bond that obbDropOldBonds() could not remove: not usable,
      // the phone asks for an authenticated link on every reconnect
      logAdd("old pairing: pair again with the code");
      hadKey = false;
    }
  }
  // No key for this phone means a pairing would follow, with two consent
  // prompts the user has to accept inside the 30 s radio window of a normal
  // wake. That never completes and puts a dialog on the phone every 5 minutes,
  // so an unbonded link is only pursued in setup mode, where the device stays
  // awake for 10 minutes (hold BOOT, or a restart) and the app's Pairing mode
  // is the natural moment.
  if (!hadKey && !setupServerAdvertising()) {
    logAdd("not paired with this phone: hold BOOT, then Pairing mode in the app");
    s_client->disconnect();
    s_nextActionMs = millis() + PAIR_FAIL_MS;
    return false;
  }
  if (!hadKey) cycleStayAwake(90000UL);   // the consent prompts must not be cut short by the window

  // Encrypted link is mandatory; first contact bonds with the passkey shown on
  // the display while the user has the pairing window open on the phone.
  // Android's GATT server sends an SMP Security Request (MITM flag) as soon as
  // a bonded device connects; with the authenticated key NimBLE answers it by
  // encrypting the link, and this call then finds the procedure in progress or
  // done. The security procedure is polled with a deadline: NimBLE's blocking
  // variant waits without limit, and a first pairing on Android needs the user
  // to type the code, which can take longer than the 30 s SMP timer while the
  // loop task's watchdog fires at 60 s. The code page is drawn meanwhile.
  bool secured = false;
  if (s_client->isConnected() && s_client->secureConnection(true)) {
    uint32_t t0 = millis();
    while (s_client->isConnected() && millis() - t0 < 50000) {
      if (s_client->getConnInfo().isEncrypted()) { secured = true; break; }
      ui.passkeyTick();
      esp_task_wdt_reset();
      delay(50);
    }
  }
  if (!secured) {
    logAdd("pairing failed (rc %d) - pairing mode + accept on phone", s_client->getLastError());
    s_client->disconnect();
    // A stale bond (the phone forgot us) makes every later attempt fail, so the
    // bond must eventually be dropped to pair afresh. But a single failure is
    // often just a transient link drop mid-handshake (remote terminated); on a
    // reconnect our stored key is still valid, so retry a few times before
    // wiping the bond and forcing the user through a new pairing window.
    if (NimBLEDevice::isBonded(s_target)) {
      if (++s_secureFails >= SECURE_FAIL_LIMIT) {
        logAdd("dropping stale bond after %d failures", s_secureFails);
        NimBLEDevice::deleteBond(s_target);
        s_secureFails = 0;
      }
    } else {
      s_secureFails = 0;   // never bonded: a fresh pairing simply did not complete
    }
    s_nextActionMs = millis() + PAIR_FAIL_MS;
    return false;
  }
  s_secureFails = 0;
  s_client->exchangeMTU();                // deferred from connect(), see above
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
  // data arrives every ~5 min: relax the link now that it is encrypted
  s_client->updateConnParams(24, 40, 4, 400);
  s_lastPacketMs = millis();
  s_connectedMs = millis();
  s_disconnected = false;
  setState(OBB_CONNECTED);
  logAdd("xDrip connected");
  if (!hadKey) {
    // a pairing just completed: the keys are in RAM, make sure they reach the
    // flash even when the phone's identity was already on record (see BleBonds.h)
    bleRepersistBond(s_client->getConnInfo().getIdAddress());
    if (cfg.debugLog) logBonds();
  }
  return true;
}

// ---- public -------------------------------------------------------------------

static void logBonds() {
  // which peers the host remembers and whether their identity key (IRK) is
  // there: without it the controller cannot resolve the phone's private
  // address and every reconnect looks like a stranger
  int n = NimBLEDevice::getNumBonds();
  for (int i = 0; i < n; i++) {
    NimBLEAddress b = NimBLEDevice::getBondedAddress(i);
    struct ble_store_key_sec k = {};
    k.peer_addr = *b.getBase();
    struct ble_store_value_sec v = {};
    int rc = ble_store_read_peer_sec(&k, &v);
    logDebug("bond %s/t%d rc%d ltk%d irk%d cs%d au%d sc%d", b.toString().c_str(), b.getType(),
             rc, v.ltk_present, v.irk_present, v.csrk_present, v.authenticated, v.sc);
  }
  if (!n) logDebug("no bond stored");
}

// Bonds made by an earlier firmware with Just Works are useless in OBB mode:
// Android's GATT server asks for an authenticated link the instant a bonded
// device connects, and NimBLE answers an unauthenticated key with a fresh
// pairing that the phone drops together with the bond. Removing a bond needs
// the controller's resolving list, which refuses while anything advertises,
// scans or is connected, so this runs right after init (bleBegin), before the
// setup server. The phone keeps its side and accepts the new pairing.
void obbDropOldBonds() {
  for (int i = NimBLEDevice::getNumBonds() - 1; i >= 0; i--) {
    NimBLEAddress a = NimBLEDevice::getBondedAddress(i);
    struct ble_store_key_sec k = {};
    k.peer_addr = *a.getBase();
    struct ble_store_value_sec v = {};
    if (ble_store_read_peer_sec(&k, &v) != 0 || !v.ltk_present || v.authenticated) continue;
    bool ok = NimBLEDevice::deleteBond(a);
    logAdd("old pairing dropped (%s): pair again", ok ? "ok" : "failed");
  }
}

void obbBegin() {
  s_enabled = true;
  if (cfg.debugLog) logBonds();
  s_nextActionMs = millis() + 500;
  setState(OBB_IDLE);
}

void obbStop() {
  if (!s_enabled) return;                 // never started (Wi-Fi source, BLE off)
  s_enabled = false;
  NimBLEDevice::getScan()->stop();
  if (s_client && s_client->isConnected()) {
    s_client->disconnect();
    delay(50);                            // let the disconnect reach the phone
  }
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
      if ((int32_t)(s_nextActionMs - now) < (int32_t)RETRY_MS) s_nextActionMs = now + RETRY_MS;
    } else if (s_statusDirty && s_statusChr) {
      s_statusDirty = false;
      NimBLEAttValue v = s_statusChr->readValue();      // main loop: blocking is fine here
      if (v.length()) setInfoLine(v.c_str(), v.length());
    } else if (now - s_lastPacketMs > SUPERVISION_MS) {
      logAdd("no OBB data for 16 min, reconnecting");
      s_client->disconnect();
      s_nextActionMs = now + RETRY_MS;
    } else if (setupServerAdvertising() && s_readingAtMs && now - s_readingAtMs > 2000) {
      // Setup mode: the link is released once the reading is in, and taken again
      // two minutes later. The app's setup client needs a link of its own: Android
      // attaches it to an existing one, where this device answers nothing, and
      // gives up after 30 s. The sleep cycle already ends its window this way;
      // the always-on mode keeps its link outside setup mode.
      logAdd("setup mode: link released for 2 min");
      s_readingAtMs = 0;
      s_client->disconnect();
      s_nextActionMs = now + SETUP_RELINK_MS;
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
