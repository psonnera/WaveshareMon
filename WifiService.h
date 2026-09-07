/*
  WifiService.h - Wi-Fi station management (non-blocking)
  (part of WaveshareMon, GPL v3, see LICENSE)

  Copyright (C) 2026 Patrick Sonnerat
*/
#ifndef WIFISERVICE_H
#define WIFISERVICE_H

#include <Arduino.h>

enum WifiState : uint8_t { WS_WIFI_OFF = 0, WS_WIFI_CONNECTING, WS_WIFI_UP, WS_WIFI_FAILED };

void wifiBegin();              // connects when the source needs it and an SSID is set
void wifiTick();
void wifiApplyConfig();        // after a config change (reconnect / disconnect)
WifiState wifiState();
const char *wifiStateName();
const char *wifiIp();
bool wifiConnected();
// set on state changes; consumed by the UI
extern volatile bool wifiStateChanged;

#endif
