/*
  BleSetupServer.cpp - device configuration over BLE
  (part of WaveshareMon, GPL v3, see LICENSE)

  Copyright (C) 2026 Patrick Sonnerat
*/
#include "BleSetupServer.h"
#include "AppConfig.h"
#include "GlucoseState.h"
#include "Alarms.h"
#include "Battery.h"
#include "BleObbClient.h"
#include "BleMiBand.h"
#include "BleBonds.h"
#include "WifiService.h"
#include "DexcomShareClient.h"
#include "LibreLinkUpClient.h"
#include "TimeService.h"
#include "EpdUi.h"
#include "PowerCycle.h"
#include "OtaUpdate.h"
void miBandOnConnect(uint16_t connHandle);     // BleMiBand.cpp
void miBandOnDisconnect(uint16_t connHandle);
#include "Log.h"
#include "Version.h"
#include <NimBLEDevice.h>
#include <ArduinoJson.h>
#include <nvs_flash.h>
#include "nimble/nimble/host/services/gatt/include/services/gatt/ble_svc_gatt.h"

static const NimBLEUUID UUID_SVC ("4d5f0001-2b8c-4a3e-9f61-7c2d9e8b5a10");
static const NimBLEUUID UUID_INFO("4d5f0002-2b8c-4a3e-9f61-7c2d9e8b5a10");
static const NimBLEUUID UUID_CFG ("4d5f0003-2b8c-4a3e-9f61-7c2d9e8b5a10");
static const NimBLEUUID UUID_CMD ("4d5f0004-2b8c-4a3e-9f61-7c2d9e8b5a10");
static const NimBLEUUID UUID_LOG ("4d5f0005-2b8c-4a3e-9f61-7c2d9e8b5a10");
static const NimBLEUUID UUID_SCAN("4d5f0006-2b8c-4a3e-9f61-7c2d9e8b5a10");

static NimBLEServer         *s_server = nullptr;
static NimBLECharacteristic *s_info = nullptr;
static NimBLECharacteristic *s_cfg = nullptr;
static NimBLECharacteristic *s_log = nullptr;
static bool                  s_advertising = false;
static volatile int          s_clients = 0;
static volatile bool         s_logSubscribed = false;
static uint32_t              s_logSent = 0;         // log entries already pushed (logTotal() based)
static volatile uint8_t      s_pendingCmd = 0;      // 1 reboot, 2 factory, 3 warn, 4 alarm, 5 refresh, 6 snooze, 7 setupoff, 8 mbforget, 9 wifiscan, 10 update, 11 updcheck
static volatile bool         s_cfgChanged = false;

// the setup app talked to us: keep the power cycle from sleeping for a while
#define APP_HOLD_MS 60000UL

// ---- JSON builders ------------------------------------------------------------

void setupBuildInfo(std::string &out) {
  JsonDocument d;
  d["fw"] = WSMON_VERSION;
  d["name"] = cfg.name();
  d["bat"] = battery.percent();
  d["mv"] = battery.millivolts();
  d["wifi"] = wifiStateName();
  d["wifierr"] = wifiFailText();                        // why the last join failed ("" = it did not)
  d["ip"] = wifiIp();
  d["src"] = cfg.source;
  d["obb"] = obbStateName();
  d["bg"] = gs.hasData ? gs.mgdl : 0;
  d["age"] = gs.hasData ? gs.minutesAgo() : -1;
  d["uptime"] = (uint32_t)(millis() / 1000);
  d["wakes"] = cycleWakes();
  d["wake"] = cycleWakeName();
  d["mac"] = NimBLEDevice::getAddress().toString();     // for xDrip's Mi Band MAC field
  d["miband"] = miBandStateName();
  d["mbkey"] = cfg.mibandKeySet != 0;
  d["live"] = gs.live;                                  // false: the panel shows its status page
  d["build"] = (uint32_t)WSMON_BUILD;                   // running build (YYYYMMDDnn, 0 = hand built)
  d["ota"] = otaStatus();                               // "" / checking / up to date / update N available / updating n% / failed: ...
  d["otabuild"] = otaLatestBuild();                     // newest build seen on the server (0 = never checked)
  char st[48];
  cycleSourceStatus(st, sizeof(st));
  d["stat"] = st;
  serializeJson(d, out);
}

void setupBuildConfig(std::string &out) {
  JsonDocument d;
  d["src"] = cfg.source;
  d["units"] = cfg.units;
  d["ssid"] = cfg.wifiSsid;
  d["pass"] = "";
  d["haspass"] = cfg.wifiPass[0] != 0;
  d["url"] = cfg.nsUrl;
  d["token"] = "";
  d["hastoken"] = cfg.nsToken[0] != 0;
  d["dxuser"] = cfg.dxUser;
  d["dxpass"] = "";
  d["hasdxpass"] = cfg.dxPass[0] != 0;
  d["dxreg"] = cfg.dxRegion;
  d["lluser"] = cfg.llUser;
  d["llpass"] = "";
  d["hasllpass"] = cfg.llPass[0] != 0;
  d["llreg"] = cfg.llRegion;
  d["llver"] = cfg.llVersion;
  d["tlsv"] = cfg.tlsVerify;
  d["tz"] = cfg.tzString;
  d["ylo"] = cfg.yellowLow;  d["yhi"] = cfg.yellowHigh;
  d["rlo"] = cfg.redLow;     d["rhi"] = cfg.redHigh;
  d["aen"] = cfg.alarmsEnabled;
  d["wlo"] = cfg.warnLow;    d["alo"] = cfg.alarmLow;
  d["whi"] = cfg.warnHigh;   d["ahi"] = cfg.alarmHigh;
  d["nor"] = cfg.noReadingsMin;
  d["wvol"] = cfg.warnVolume; d["avol"] = cfg.alarmVolume;
  d["arep"] = cfg.alarmRepeatMin;
  d["snoz"] = cfg.snoozeMin;
  d["t24"] = cfg.timeFormat24;
  d["dmy"] = cfg.dateFormatDMY;
  d["sline"] = cfg.obbStatusLine;
  d["name"] = cfg.deviceName;
  serializeJson(d, out);
}

template <typename T>
static void getNum(JsonDocument &d, const char *key, T &field, long lo, long hi) {
  if (d[key].isNull()) return;
  long v = d[key].as<long>();
  if (v < lo) v = lo;
  if (v > hi) v = hi;
  field = (T)v;
}
static void getStr(JsonDocument &d, const char *key, char *field, size_t len) {
  if (!d[key].is<const char *>()) return;
  strlcpy(field, d[key].as<const char *>(), len);
}

static bool applyConfig(const char *json, size_t len) {
  JsonDocument d;
  if (deserializeJson(d, json, len)) { logAdd("config: bad JSON"); return false; }
  uint8_t oldSrc = cfg.source;
  getNum(d, "src", cfg.source, 0, SRC_MAX);
  // a new source, or new credentials for the active one, must prove itself
  // with a reading before the panel leaves its status page
  bool sourceTouched = cfg.source != oldSrc ||
      (SRC_IS_WIFI(cfg.source) && (d["ssid"].is<const char *>() || d["pass"].is<const char *>())) ||
      (cfg.source == SRC_NIGHTSCOUT && (d["url"].is<const char *>() || d["token"].is<const char *>())) ||
      (cfg.source == SRC_DEXCOM && (d["dxuser"].is<const char *>() || d["dxpass"].is<const char *>() || !d["dxreg"].isNull())) ||
      (cfg.source == SRC_LIBRE && (d["lluser"].is<const char *>() || d["llpass"].is<const char *>() || d["llreg"].is<const char *>()));
  if (sourceTouched) gs.markSourceChanged();
  getNum(d, "units", cfg.units, 0, 1);
  // the phone's clock, for sources that never deliver the time (Mi Band)
  if (d["now"].is<long long>()) {
    long long now = d["now"].as<long long>();
    if (now > 1600000000LL) timeService.setFromUtc((time_t)now);
  }
  getStr(d, "ssid", cfg.wifiSsid, sizeof(cfg.wifiSsid));
  getStr(d, "pass", cfg.wifiPass, sizeof(cfg.wifiPass));
  getStr(d, "url", cfg.nsUrl, sizeof(cfg.nsUrl));
  getStr(d, "token", cfg.nsToken, sizeof(cfg.nsToken));
  // cloud accounts: a changed login invalidates the cached session
  char oldUser[65], oldPass[64]; uint8_t oldReg;
  strlcpy(oldUser, cfg.dxUser, sizeof(oldUser)); strlcpy(oldPass, cfg.dxPass, sizeof(oldPass)); oldReg = cfg.dxRegion;
  getStr(d, "dxuser", cfg.dxUser, sizeof(cfg.dxUser));
  getStr(d, "dxpass", cfg.dxPass, sizeof(cfg.dxPass));
  getNum(d, "dxreg", cfg.dxRegion, 0, 2);
  if (strcmp(oldUser, cfg.dxUser) || strcmp(oldPass, cfg.dxPass) || oldReg != cfg.dxRegion) dxForgetSession();
  char oldReg2[8];
  strlcpy(oldUser, cfg.llUser, sizeof(oldUser)); strlcpy(oldPass, cfg.llPass, sizeof(oldPass)); strlcpy(oldReg2, cfg.llRegion, sizeof(oldReg2));
  getStr(d, "lluser", cfg.llUser, sizeof(cfg.llUser));
  getStr(d, "llpass", cfg.llPass, sizeof(cfg.llPass));
  getStr(d, "llreg", cfg.llRegion, sizeof(cfg.llRegion));
  getStr(d, "llver", cfg.llVersion, sizeof(cfg.llVersion));
  if (strcmp(oldUser, cfg.llUser) || strcmp(oldPass, cfg.llPass) || strcmp(oldReg2, cfg.llRegion)) llForgetSession();
  getNum(d, "tlsv", cfg.tlsVerify, 0, 1);
  getStr(d, "tz", cfg.tzString, sizeof(cfg.tzString));
  getNum(d, "ylo", cfg.yellowLow, 20, 600);  getNum(d, "yhi", cfg.yellowHigh, 20, 600);
  getNum(d, "rlo", cfg.redLow, 20, 600);     getNum(d, "rhi", cfg.redHigh, 20, 600);
  getNum(d, "aen", cfg.alarmsEnabled, 0, 1);
  getNum(d, "wlo", cfg.warnLow, 20, 600);    getNum(d, "alo", cfg.alarmLow, 20, 600);
  getNum(d, "whi", cfg.warnHigh, 20, 600);   getNum(d, "ahi", cfg.alarmHigh, 20, 600);
  getNum(d, "nor", cfg.noReadingsMin, 5, 1440);
  getNum(d, "wvol", cfg.warnVolume, 0, 100); getNum(d, "avol", cfg.alarmVolume, 0, 100);
  getNum(d, "arep", cfg.alarmRepeatMin, 1, 120);
  getNum(d, "snoz", cfg.snoozeMin, 1, 240);
  getNum(d, "t24", cfg.timeFormat24, 0, 1);
  getNum(d, "dmy", cfg.dateFormatDMY, 0, 1);
  getNum(d, "sline", cfg.obbStatusLine, 0, 1);
  getStr(d, "name", cfg.deviceName, sizeof(cfg.deviceName));
  cfg.save();
  return true;
}

bool setupApplyConfig(const char *json, size_t len, const char *who) {
  if (!applyConfig(json, len)) return false;
  logAdd("config updated by %s", who);
  s_cfgChanged = true;                  // setupServerTick() applies the rest (Wi-Fi, TZ, redraw)
  return true;
}

// the Command characteristic's words -> the deferred command run by setupServerTick()
static uint8_t commandCode(const char *s) {
  if (!strcmp(s, "reboot"))    return 1;
  if (!strcmp(s, "factory"))   return 2;
  if (!strcmp(s, "testwarn"))  return 3;
  if (!strcmp(s, "testalarm")) return 4;
  if (!strcmp(s, "refresh"))   return 5;
  if (!strcmp(s, "snooze"))    return 6;
  if (!strcmp(s, "setupoff"))  return 7;
  if (!strcmp(s, "mbforget"))  return 8;
  if (!strcmp(s, "wifiscan"))  return 9;
  if (!strcmp(s, "update"))    return 10;     // check the repository and install a newer build
  if (!strcmp(s, "updcheck"))  return 11;     // check only
  return 0;
}

bool setupCommand(const char *cmd) {
  uint8_t c = commandCode(cmd);
  if (!c) return false;
  logAdd("cmd: %s", cmd);
  cycleStayAwake(APP_HOLD_MS);
  s_pendingCmd = c;
  return true;
}

// ---- callbacks (NimBLE host task) ---------------------------------------------

// advertising wanted: setup mode, or the Mi Band source waiting for xDrip
static bool wantAdvertising() {
  return s_advertising || cfg.source == SRC_MIBAND;
}


static uint16_t s_repersistHandle = BLE_HS_CONN_HANDLE_NONE;
static uint32_t s_repersistAtMs = 0;

class ServerCb : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *, NimBLEConnInfo &info) override {
    s_clients++;
    logAdd("BLE client connected");
    // Android keeps a bonded peer's GATT table cached across connections; the
    // table here depends on the configured source (Mi Band services or not),
    // so tell subscribed bonded clients to discover again.
    ble_svc_gatt_changed(0x0001, 0xffff);
    miBandOnConnect(info.getConnHandle());
  }
  void onAuthenticationComplete(NimBLEConnInfo &info) override {
    // the phone's keys arrive after this event; rewrite the stored bond a
    // little later from the loop (see BleBonds.h)
    if (info.isEncrypted() && info.isBonded()) {
      s_repersistHandle = info.getConnHandle();
      s_repersistAtMs = millis() + 3000;
    }
  }
  void onDisconnect(NimBLEServer *, NimBLEConnInfo &info, int) override {
    if (s_clients > 0) s_clients--;
    s_logSubscribed = false;
    logAdd("BLE client disconnected");
    miBandOnDisconnect(info.getConnHandle());
    if (wantAdvertising()) NimBLEDevice::startAdvertising();
  }
} s_serverCb;

class InfoCb : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic *c, NimBLEConnInfo &) override {
    cycleStayAwake(APP_HOLD_MS);
    std::string s; setupBuildInfo(s); c->setValue(s);
  }
} s_infoCb;

class CfgCb : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic *c, NimBLEConnInfo &) override {
    cycleStayAwake(APP_HOLD_MS);
    std::string s; setupBuildConfig(s); c->setValue(s);
  }
  void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &) override {
    NimBLEAttValue v = c->getValue();
    setupApplyConfig((const char *)v.data(), v.length(), "app");
  }
} s_cfgCb;

class CmdCb : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &) override {
    std::string s = c->getValue();
    if (!setupCommand(s.c_str())) logAdd("cmd: unknown %s", s.c_str());
  }
} s_cmdCb;

class WifiScanCb : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic *c, NimBLEConnInfo &) override {
    cycleStayAwake(APP_HOLD_MS);
    char json[512];
    wifiScanJson(json, sizeof(json));
    std::string s(json);
    c->setValue(s);
    logDebug("scan read: %u bytes", (unsigned)s.size());
  }
} s_wifiScanCb;

class LogCb : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic *, NimBLEConnInfo &, uint16_t subValue) override {
    cycleStayAwake(APP_HOLD_MS);
    s_logSubscribed = subValue != 0;
    s_logSent = 0;            // replay the whole buffer to a new subscriber
  }
} s_logCb;

// ---- public -------------------------------------------------------------------

void setupServerBegin() {
  s_server = NimBLEDevice::createServer();
  s_server->setCallbacks(&s_serverCb);
  s_server->advertiseOnDisconnect(false);
  NimBLEService *svc = s_server->createService(UUID_SVC);
  s_info = svc->createCharacteristic(UUID_INFO, NIMBLE_PROPERTY::READ, 512);
  s_info->setCallbacks(&s_infoCb);
  s_cfg = svc->createCharacteristic(UUID_CFG,
            NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE |
            NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::WRITE_ENC, 512);
  s_cfg->setCallbacks(&s_cfgCb);
  NimBLECharacteristic *cmd = svc->createCharacteristic(UUID_CMD,
            NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC, 32);
  cmd->setCallbacks(&s_cmdCb);
  s_log = svc->createCharacteristic(UUID_LOG, NIMBLE_PROPERTY::NOTIFY, 64);
  s_log->setCallbacks(&s_logCb);
  NimBLECharacteristic *scan = svc->createCharacteristic(UUID_SCAN, NIMBLE_PROPERTY::READ, 512);
  scan->setCallbacks(&s_wifiScanCb);
  scan->setValue(std::string("{\"scan\":\"idle\"}"));
  svc->start();

  // 128-bit service UUID in the advertisement, name in the scan response
  // (both do not fit in the 31-byte advertising packet). In Mi Band mode the
  // Huami service UUID (16-bit) and the name "MI Band 2" are what xDrip looks
  // for; the setup UUID stays in so the app can still find the device.
  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  NimBLEAdvertisementData advData;
  advData.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  if (cfg.source == SRC_MIBAND) advData.addServiceUUID(NimBLEUUID((uint16_t)0xFEE0));
  advData.addServiceUUID(UUID_SVC);
  NimBLEAdvertisementData scanData;
  scanData.setName(cfg.source == SRC_MIBAND ? "MI Band 2" : cfg.name());
  adv->setAdvertisementData(advData);
  adv->setScanResponseData(scanData);
}

void setupServerAdvertise(bool on) {
  if (on == s_advertising) return;
  s_advertising = on;
  if (on) {
    NimBLEDevice::startAdvertising();
    logAdd("setup mode: %s", cfg.name());
  } else {
    if (!wantAdvertising()) NimBLEDevice::stopAdvertising();
    logAdd("setup mode off");
  }
  ui.requestRedraw();
}

bool setupServerAdvertising() { return s_advertising; }
bool setupServerClientConnected() { return s_clients > 0; }

void setupServerTick() {
  if (s_repersistHandle != BLE_HS_CONN_HANDLE_NONE && (int32_t)(millis() - s_repersistAtMs) >= 0) {
    uint16_t h = s_repersistHandle;
    s_repersistHandle = BLE_HS_CONN_HANDLE_NONE;
    NimBLEConnInfo ci = s_server->getPeerInfoByHandle(h);
    if (ci.getConnHandle() == h) bleRepersistBond(ci.getIdAddress());
  }
  // the controller refuses to (re)start advertising while a central connection
  // is being established (rc 519): keep retrying while setup mode is wanted
  static uint32_t lastAdvRetryMs = 0;
  if (wantAdvertising() && s_clients == 0 && !NimBLEDevice::getAdvertising()->isAdvertising() &&
      millis() - lastAdvRetryMs > 3000) {
    lastAdvRetryMs = millis();
    NimBLEDevice::startAdvertising();
  }

  // push new log lines to a subscribed app (oldest first)
  if (s_logSubscribed && s_log) {
    uint32_t total = logTotal();                 // entries ever logged
    if (s_logSent > total) s_logSent = 0;
    if (total - s_logSent > LOG_ENTRIES) s_logSent = total - LOG_ENTRIES;
    while (s_logSent < total) {
      const LogEntry *e = logGet((int)(total - 1 - s_logSent));   // oldest unsent first
      s_logSent++;
      if (!e) continue;
      char line[LOG_LINE_LEN + 16], stamp[16];
      logStamp(e, stamp, sizeof(stamp));
      snprintf(line, sizeof(line), "%s %s", stamp, e->text);
      s_log->setValue((uint8_t *)line, strlen(line));
      s_log->notify();
      delay(5);
    }
  }

  if (s_cfgChanged) {
    s_cfgChanged = false;
    cfg.markConfigured();                 // the power cycle may start once setup ends
    cycleStayAwake(SETUP_WINDOW_MS);      // give the app time for more changes
    timeService.applyTz();
    wifiApplyConfig();
    ui.requestRedraw();
  }

  uint8_t c = s_pendingCmd;
  if (!c) return;
  s_pendingCmd = 0;
  switch (c) {
    case 1: delay(300); ESP.restart(); break;
    case 2:
      cfg.factoryReset();
      NimBLEDevice::deleteAllBonds();
      nvs_flash_deinit(); nvs_flash_erase();
      delay(300); ESP.restart();
      break;
    case 3: alarms.testSound(false); break;
    case 4: alarms.testSound(true); break;
    case 5: ui.requestRedraw(); break;
    case 6: alarms.snooze(); break;
    case 7: setupServerAdvertise(false); break;
    case 8: miBandForgetKey(); break;
    case 9: wifiScanStart(); break;
    case 10: otaRequest(true); break;
    case 11: otaRequest(false); break;
  }
}

void setupServerDropClients() {
  if (!s_server || s_clients == 0) return;
  for (auto h : s_server->getPeerDevices()) s_server->disconnect(h);
  delay(50);
}
