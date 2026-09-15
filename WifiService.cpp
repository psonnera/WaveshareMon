/*
  WifiService.cpp - Wi-Fi station management (non-blocking)
  (part of WaveshareMon, GPL v3, see LICENSE)

  The access point's channel and BSSID are cached in RTC memory so a wake from
  deep sleep joins without a scan (about a second instead of several). When the
  fast join fails the normal scan-and-join path follows.

  Copyright (C) 2026 Patrick Sonnerat
*/
#include <esp_attr.h>
#include "WifiService.h"
#include "AppConfig.h"
#include "TimeService.h"
#include "Log.h"
#include <WiFi.h>
#include <ArduinoJson.h>

#define CONNECT_TIMEOUT_MS      30000
#define FAST_CONNECT_TIMEOUT_MS  8000
#define RETRY_MS                60000
#define SCAN_MAX_NETS           12
#define SCAN_MAX_JSON           480
#define SCAN_JSON_LEN           512

volatile bool wifiStateChanged = false;

static WifiState s_state = WS_WIFI_OFF;
static uint32_t  s_startMs = 0;
static uint32_t  s_retryMs = 0;
static char      s_ip[20] = "";
static char      s_failText[32] = "";
static bool      s_wanted = false;
static bool      s_fastTry = false;      // current attempt uses the cached channel/BSSID
static bool      s_fastFailed = false;   // fast path failed once this wake: do not retry it
static bool      s_eventHooked = false;
static volatile uint8_t s_lastReason = 0;   // wifi_err_reason_t of the last STA disconnect

// scan for the setup app
static bool        s_scanActive = false;
static bool        s_scanRadioOn = false;   // the radio was off: switch it off again afterwards
static char        s_scanJson[SCAN_JSON_LEN] = "{\"scan\":\"idle\"}";
// the setup server reads the JSON from the NimBLE host task while the loop task rewrites it
static portMUX_TYPE s_scanMux = portMUX_INITIALIZER_UNLOCKED;


struct WifiRtc {
  uint32_t magic;
  uint8_t  bssid[6];
  uint8_t  channel;
  char     ssid[33];
};
#define WIFI_MAGIC 0x57494631UL
RTC_DATA_ATTR static WifiRtc s_rtc;

static void setScanJson(const char *json) {
  portENTER_CRITICAL(&s_scanMux);
  strlcpy(s_scanJson, json, sizeof(s_scanJson));
  portEXIT_CRITICAL(&s_scanMux);
}

static void setState(WifiState s) {
  if (s == s_state) return;
  s_state = s;
  wifiStateChanged = true;
}

static bool cacheValid() {
  return s_rtc.magic == WIFI_MAGIC && s_rtc.channel != 0 &&
         strcmp(s_rtc.ssid, cfg.wifiSsid) == 0;
}

// the disconnect reason is the only thing that tells "no such network" from
// "wrong password"; it arrives on the Wi-Fi event task
static void onWifiEvent(arduino_event_id_t ev, arduino_event_info_t info) {
  if (ev == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) s_lastReason = info.wifi_sta_disconnected.reason;
}

static void hookEvents() {
  if (s_eventHooked) return;
  s_eventHooked = true;
  WiFi.onEvent(onWifiEvent, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
}

static void failTextFor(uint8_t reason, char *out, size_t len) {
  switch (reason) {
    case WIFI_REASON_NO_AP_FOUND:                                  // 201
      strlcpy(out, "network not found", len); break;
    case WIFI_REASON_AUTH_EXPIRE:                                  // 2
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:                       // 15
    case WIFI_REASON_AUTH_FAIL:                                    // 202
    case WIFI_REASON_HANDSHAKE_TIMEOUT:                            // 204
      strlcpy(out, "wrong password", len); break;
    case 0:
      strlcpy(out, "no reply", len); break;
    default:
      snprintf(out, len, "failed (%u)", reason); break;
  }
}

static void startConnect() {
  hookEvents();
  WiFi.persistent(false);                // no NVS writes on every join
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(cfg.name());
  WiFi.setAutoReconnect(true);
  s_lastReason = 0;
  s_fastTry = cacheValid() && !s_fastFailed;
  if (s_fastTry) WiFi.begin(cfg.wifiSsid, cfg.wifiPass, s_rtc.channel, s_rtc.bssid);
  else           WiFi.begin(cfg.wifiSsid, cfg.wifiPass);
  s_startMs = millis();
  setState(WS_WIFI_CONNECTING);
  logDebug("wifi: %s join %s", s_fastTry ? "fast" : "full", cfg.wifiSsid);
}

void wifiBegin() {
  wifiApplyConfig();
}

void wifiApplyConfig() {
  bool want = SRC_IS_WIFI(cfg.source) && cfg.wifiConfigured();
  s_failText[0] = 0;
  if (!want) {
    if (s_wanted) {
      WiFi.disconnect(!s_scanActive /*radio off, unless a scan runs*/);
      logAdd("wifi: off");
    }
    s_wanted = false;
    setState(WS_WIFI_OFF);
    return;
  }
  // (re)connect with the current credentials
  if (s_wanted) WiFi.disconnect(!s_scanActive);
  s_wanted = true;
  s_fastFailed = false;
  if (s_scanActive) return;              // the join starts when the scan is over
  startConnect();
}

void wifiSleep() {
  if (!s_wanted) return;
  WiFi.disconnect(false);
  WiFi.mode(WIFI_OFF);
  s_wanted = false;
  setState(WS_WIFI_OFF);
}

// ---- scan -----------------------------------------------------------------------

void wifiScanStart() {
  if (s_scanActive) return;
  hookEvents();
  if (WiFi.getMode() == WIFI_OFF) {
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    s_scanRadioOn = true;
  }
  // the driver refuses to scan while a join is in progress: give the join up,
  // it is restarted after the scan
  if (s_state == WS_WIFI_CONNECTING) WiFi.disconnect(false);
  if (WiFi.scanNetworks(true /*async*/, false /*hidden*/) == WIFI_SCAN_FAILED) {
    logAdd("wifi scan: failed to start");
    setScanJson("{\"scan\":\"idle\"}");
    if (s_scanRadioOn) { WiFi.mode(WIFI_OFF); s_scanRadioOn = false; }
    return;
  }
  s_scanActive = true;
  setScanJson("{\"scan\":\"busy\"}");
  logAdd("wifi scan...");
}

bool wifiScanBusy() { return s_scanActive; }
void wifiScanJson(char *out, size_t len) {
  portENTER_CRITICAL(&s_scanMux);
  strlcpy(out, s_scanJson, len);
  portEXIT_CRITICAL(&s_scanMux);
}

static void scanFinish(int16_t n) {
  JsonDocument d;
  d["scan"] = "done";
  JsonArray nets = d["nets"].to<JsonArray>();
  int kept = 0;
  for (int i = 0; i < n && kept < SCAN_MAX_NETS; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.isEmpty()) continue;
    bool dup = false;                     // repeaters: keep the strongest only
    for (JsonObject o : nets) if (ssid == (const char *)o["s"]) { dup = true; break; }
    if (dup) continue;
    JsonObject o = nets.add<JsonObject>();
    o["s"] = ssid;
    o["r"] = WiFi.RSSI(i);
    o["c"] = WiFi.channel(i);
    o["e"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? 1 : 0;
    if (measureJson(d) > SCAN_MAX_JSON) { nets.remove(nets.size() - 1); break; }
    kept++;
    Serial.printf("[wifi] %2d  %4d dBm  ch %2d  %s%s\n", kept, (int)WiFi.RSSI(i), (int)WiFi.channel(i),
                  ssid.c_str(), WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "  (open)" : "");
  }
  char json[SCAN_JSON_LEN];
  serializeJson(d, json, sizeof(json));
  setScanJson(json);
  WiFi.scanDelete();
  logAdd("wifi scan: %d network%s", kept, kept == 1 ? "" : "s");
}

static void scanTick() {
  if (!s_scanActive) return;
  int16_t n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return;
  s_scanActive = false;
  if (n < 0) { setScanJson("{\"scan\":\"idle\"}"); logAdd("wifi scan: failed"); }
  else scanFinish(n);
  if (s_scanRadioOn) {
    s_scanRadioOn = false;
    if (!s_wanted) WiFi.mode(WIFI_OFF);
  }
  // resume the join the scan interrupted (or start the one a config write asked for)
  if (s_wanted && s_state != WS_WIFI_UP) startConnect();
}

// ---- tick -----------------------------------------------------------------------

void wifiTick() {
  scanTick();
  if (!s_wanted || s_scanActive) return;
  uint32_t now = millis();
  wl_status_t st = WiFi.status();
  switch (s_state) {
    case WS_WIFI_CONNECTING:
      if (st == WL_CONNECTED) {
        strlcpy(s_ip, WiFi.localIP().toString().c_str(), sizeof(s_ip));
        s_failText[0] = 0;
        setState(WS_WIFI_UP);
        logAdd("wifi: %s (%lu ms)", s_ip, (unsigned long)(now - s_startMs));
        // remember the AP for the next wake
        s_rtc.magic = WIFI_MAGIC;
        s_rtc.channel = (uint8_t)WiFi.channel();
        memcpy(s_rtc.bssid, WiFi.BSSID(), 6);
        strlcpy(s_rtc.ssid, cfg.wifiSsid, sizeof(s_rtc.ssid));
        timeService.startNtp();
      } else if (s_fastTry && now - s_startMs > FAST_CONNECT_TIMEOUT_MS) {
        // the AP moved channel or is out of reach on the cached BSSID: scan
        s_fastFailed = true;
        WiFi.disconnect(true);
        startConnect();
      } else if (now - s_startMs > CONNECT_TIMEOUT_MS) {
        failTextFor(s_lastReason, s_failText, sizeof(s_failText));
        setState(WS_WIFI_FAILED);
        s_retryMs = now;
        logAdd("wifi: %s (st %d, r %u)", s_failText, (int)st, (unsigned)s_lastReason);
        WiFi.disconnect(true);
      }
      break;
    case WS_WIFI_UP:
      if (st != WL_CONNECTED) {
        setState(WS_WIFI_CONNECTING);
        s_startMs = now;
        logAdd("wifi: lost, reconnecting");
      }
      break;
    case WS_WIFI_FAILED:
      if (now - s_retryMs > RETRY_MS) startConnect();
      break;
    default:
      break;
  }
}

WifiState wifiState() { return s_state; }
bool wifiConnected() { return s_state == WS_WIFI_UP && WiFi.status() == WL_CONNECTED; }
const char *wifiIp() { return s_state == WS_WIFI_UP ? s_ip : ""; }
const char *wifiFailText() { return s_failText; }
const char *wifiStateName() {
  switch (s_state) {
    case WS_WIFI_CONNECTING: return "connecting";
    case WS_WIFI_UP:         return "connected";
    case WS_WIFI_FAILED:     return "failed";
    default:              return "off";
  }
}
