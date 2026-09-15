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
// keep the station up although the source does not need it (firmware update
// on a Bluetooth source); needs an SSID. off releases it again.
void wifiHold(bool on);
void wifiSleep();              // radio off before deep sleep
WifiState wifiState();
const char *wifiStateName();
// why the last join failed ("" while nothing failed): "network not found",
// "wrong password", "failed (reason)"
const char *wifiFailText();
const char *wifiIp();
bool wifiConnected();
// set on state changes; consumed by the UI
extern volatile bool wifiStateChanged;

// Network scan for the setup app (the radio sees 2.4 GHz only, so the list
// is exactly what the device can join). Asynchronous: start, then poll the
// result. The JSON is {"scan":"idle|busy|done","nets":[{"s":ssid,"r":rssi,
// "c":channel,"e":0|1}, ...]}, strongest first, one entry per SSID.
void wifiScanStart();
bool wifiScanBusy();
// copies the result JSON into out (safe to call from the NimBLE host task)
void wifiScanJson(char *out, size_t len);

#endif
