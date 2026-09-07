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
#include "WifiService.h"
#include "TimeService.h"
#include "EpdUi.h"
#include "Log.h"
#include "Version.h"
#include <NimBLEDevice.h>
#include <ArduinoJson.h>
#include <nvs_flash.h>

static const NimBLEUUID UUID_SVC ("4d5f0001-2b8c-4a3e-9f61-7c2d9e8b5a10");
static const NimBLEUUID UUID_INFO("4d5f0002-2b8c-4a3e-9f61-7c2d9e8b5a10");
static const NimBLEUUID UUID_CFG ("4d5f0003-2b8c-4a3e-9f61-7c2d9e8b5a10");
static const NimBLEUUID UUID_CMD ("4d5f0004-2b8c-4a3e-9f61-7c2d9e8b5a10");
static const NimBLEUUID UUID_LOG ("4d5f0005-2b8c-4a3e-9f61-7c2d9e8b5a10");

static NimBLEServer         *s_server = nullptr;
static NimBLECharacteristic *s_info = nullptr;
static NimBLECharacteristic *s_cfg = nullptr;
static NimBLECharacteristic *s_log = nullptr;
static bool                  s_advertising = false;
static volatile int          s_clients = 0;
static volatile bool         s_logSubscribed = false;
static uint32_t              s_logSent = 0;         // log entries already pushed (logTotal() based)
static volatile uint8_t      s_pendingCmd = 0;      // 1 reboot, 2 factory, 3 warn, 4 alarm, 5 refresh, 6 snooze, 7 setupoff
static volatile bool         s_cfgChanged = false;

// ---- JSON builders ------------------------------------------------------------

static void buildInfo(std::string &out) {
  JsonDocument d;
  d["fw"] = WSMON_VERSION;
  d["name"] = cfg.name();
  d["bat"] = battery.percent();
  d["mv"] = battery.millivolts();
  d["wifi"] = wifiStateName();
  d["ip"] = wifiIp();
  d["src"] = cfg.source;
  d["obb"] = obbStateName();
  d["bg"] = gs.hasData ? gs.mgdl : 0;
  d["age"] = gs.hasData ? gs.minutesAgo() : -1;
  d["uptime"] = (uint32_t)(millis() / 1000);
  serializeJson(d, out);
}

static void buildConfig(std::string &out) {
  JsonDocument d;
  d["src"] = cfg.source;
  d["units"] = cfg.units;
  d["ssid"] = cfg.wifiSsid;
  d["pass"] = "";
  d["haspass"] = cfg.wifiPass[0] != 0;
  d["url"] = cfg.nsUrl;
  d["token"] = "";
  d["hastoken"] = cfg.nsToken[0] != 0;
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
  getNum(d, "src", cfg.source, 0, 1);
  getNum(d, "units", cfg.units, 0, 1);
  getStr(d, "ssid", cfg.wifiSsid, sizeof(cfg.wifiSsid));
  getStr(d, "pass", cfg.wifiPass, sizeof(cfg.wifiPass));
  getStr(d, "url", cfg.nsUrl, sizeof(cfg.nsUrl));
  getStr(d, "token", cfg.nsToken, sizeof(cfg.nsToken));
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

// ---- callbacks (NimBLE host task) ---------------------------------------------

class ServerCb : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *, NimBLEConnInfo &info) override {
    s_clients++;
    logAdd("setup app connected");
  }
  void onDisconnect(NimBLEServer *, NimBLEConnInfo &, int) override {
    if (s_clients > 0) s_clients--;
    s_logSubscribed = false;
    logAdd("setup app disconnected");
    if (s_advertising) NimBLEDevice::startAdvertising();
  }
} s_serverCb;

class InfoCb : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic *c, NimBLEConnInfo &) override {
    std::string s; buildInfo(s); c->setValue(s);
  }
} s_infoCb;

class CfgCb : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic *c, NimBLEConnInfo &) override {
    std::string s; buildConfig(s); c->setValue(s);
  }
  void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &) override {
    NimBLEAttValue v = c->getValue();
    if (applyConfig((const char *)v.data(), v.length())) {
      logAdd("config updated by app");
      s_cfgChanged = true;
    }
  }
} s_cfgCb;

class CmdCb : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &) override {
    std::string s = c->getValue();
    logAdd("cmd: %s", s.c_str());
    if      (s == "reboot")    s_pendingCmd = 1;
    else if (s == "factory")   s_pendingCmd = 2;
    else if (s == "testwarn")  s_pendingCmd = 3;
    else if (s == "testalarm") s_pendingCmd = 4;
    else if (s == "refresh")   s_pendingCmd = 5;
    else if (s == "snooze")    s_pendingCmd = 6;
    else if (s == "setupoff")  s_pendingCmd = 7;
  }
} s_cmdCb;

class LogCb : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic *, NimBLEConnInfo &, uint16_t subValue) override {
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
  s_info = svc->createCharacteristic(UUID_INFO, NIMBLE_PROPERTY::READ, 256);
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
  svc->start();

  // 128-bit service UUID in the advertisement, name in the scan response
  // (both do not fit in the 31-byte advertising packet)
  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  NimBLEAdvertisementData advData;
  advData.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  advData.addServiceUUID(UUID_SVC);
  NimBLEAdvertisementData scanData;
  scanData.setName(cfg.name());
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
    NimBLEDevice::stopAdvertising();
    logAdd("setup mode off");
  }
  ui.requestRedraw();
}

bool setupServerAdvertising() { return s_advertising; }
bool setupServerClientConnected() { return s_clients > 0; }

void setupServerTick() {
  // the controller refuses to (re)start advertising while a central connection
  // is being established (rc 519): keep retrying while setup mode is wanted
  static uint32_t lastAdvRetryMs = 0;
  if (s_advertising && s_clients == 0 && !NimBLEDevice::getAdvertising()->isAdvertising() &&
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
      char line[LOG_LINE_LEN + 12];
      snprintf(line, sizeof(line), "%6lus %s", (unsigned long)(e->ms / 1000), e->text);
      s_log->setValue((uint8_t *)line, strlen(line));
      s_log->notify();
      delay(5);
    }
  }

  if (s_cfgChanged) {
    s_cfgChanged = false;
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
  }
}
