/*
  WifiService.cpp - Wi-Fi station management (non-blocking)
  (part of WaveshareMon, GPL v3, see LICENSE)

  Copyright (C) 2026 Patrick Sonnerat
*/
#include "WifiService.h"
#include "AppConfig.h"
#include "TimeService.h"
#include "Log.h"
#include <WiFi.h>

volatile bool wifiStateChanged = false;

static WifiState s_state = WS_WIFI_OFF;
static uint32_t  s_startMs = 0;
static uint32_t  s_retryMs = 0;
static char      s_ip[20] = "";
static bool      s_wanted = false;

#define CONNECT_TIMEOUT_MS 30000
#define RETRY_MS           60000

static void setState(WifiState s) {
  if (s == s_state) return;
  s_state = s;
  wifiStateChanged = true;
}

static void startConnect() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(cfg.name());
  WiFi.setAutoReconnect(true);
  WiFi.begin(cfg.wifiSsid, cfg.wifiPass);
  s_startMs = millis();
  setState(WS_WIFI_CONNECTING);
  logAdd("wifi: connecting to %s", cfg.wifiSsid);
}

void wifiBegin() {
  wifiApplyConfig();
}

void wifiApplyConfig() {
  bool want = cfg.source == SRC_NIGHTSCOUT && cfg.wifiConfigured();
  if (!want) {
    if (s_wanted) {
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      logAdd("wifi: off");
    }
    s_wanted = false;
    setState(WS_WIFI_OFF);
    return;
  }
  // (re)connect with the current credentials
  if (s_wanted) WiFi.disconnect(true);
  s_wanted = true;
  startConnect();
}

void wifiTick() {
  if (!s_wanted) return;
  uint32_t now = millis();
  wl_status_t st = WiFi.status();
  switch (s_state) {
    case WS_WIFI_CONNECTING:
      if (st == WL_CONNECTED) {
        strlcpy(s_ip, WiFi.localIP().toString().c_str(), sizeof(s_ip));
        setState(WS_WIFI_UP);
        logAdd("wifi: %s", s_ip);
        timeService.startNtp();
      } else if (now - s_startMs > CONNECT_TIMEOUT_MS) {
        setState(WS_WIFI_FAILED);
        s_retryMs = now;
        logAdd("wifi: failed (%d)", (int)st);
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
const char *wifiStateName() {
  switch (s_state) {
    case WS_WIFI_CONNECTING: return "connecting";
    case WS_WIFI_UP:         return "connected";
    case WS_WIFI_FAILED:     return "failed";
    default:              return "off";
  }
}
