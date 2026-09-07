/*
  AppConfig.h - persisted settings (NVS)
  (part of WaveshareMon, GPL v3, see LICENSE; derived from M5Stack_xDripMon)

  Copyright (C) 2026 Patrick Sonnerat
*/
#ifndef APPCONFIG_H
#define APPCONFIG_H

#include <Arduino.h>

// data sources
#define SRC_OBB        0   // xDrip Open Bluetooth Broadcast (BLE)
#define SRC_NIGHTSCOUT 1   // Nightscout over Wi-Fi

// units
#define UNITS_MGDL 0
#define UNITS_MMOL 1

#define MGDL_TO_MMOL(x) ((x) / 18.0f)

struct AppConfig {
  uint8_t  source          = SRC_OBB;
  uint8_t  units           = UNITS_MGDL;
  // display thresholds (colours), canonical mg/dL
  uint16_t yellowLow       = 70;
  uint16_t yellowHigh      = 180;
  uint16_t redLow          = 55;
  uint16_t redHigh         = 250;
  // sound thresholds, canonical mg/dL
  uint8_t  alarmsEnabled   = 1;
  uint16_t warnLow         = 70;
  uint16_t alarmLow        = 55;
  uint16_t warnHigh        = 180;
  uint16_t alarmHigh       = 250;
  uint16_t noReadingsMin   = 30;   // warn when data older than this
  uint8_t  warnVolume      = 30;   // 0-100
  uint8_t  alarmVolume     = 80;   // 0-100
  uint8_t  alarmRepeatMin  = 5;
  uint8_t  snoozeMin       = 30;
  // display
  uint8_t  timeFormat24    = 1;    // 1 = 24h, 0 = 12h am/pm
  uint8_t  dateFormatDMY   = 1;    // 1 = d.m., 0 = m/d
  uint8_t  debugLog        = 0;
  // time: POSIX TZ string wins when set, else fixed offset (OBB status / manual)
  char     tzString[48]    = "CET-1CEST,M3.5.0,M10.5.0/3";
  int32_t  tzOffsetSec     = 3600;
  // Wi-Fi + Nightscout
  char     wifiSsid[33]    = "";
  char     wifiPass[64]    = "";
  char     nsUrl[128]      = "";
  char     nsToken[64]     = "";
  // OBB options
  uint8_t  obbStatusLine   = 0;    // subscribe to the optional status line
  // custom name (empty = WaveshareMon-XXXX from the MAC)
  char     deviceName[25]  = "";

  // true after load() when no valid config was found (fresh device / factory
  // reset). Not persisted.
  bool     firstRun        = false;

  void load();
  void save();
  void factoryReset();

  bool isMgdl() const { return units == UNITS_MGDL; }
  bool wifiConfigured() const { return wifiSsid[0] != 0; }
  bool nsConfigured() const { return nsUrl[0] != 0; }
  // effective BLE / mDNS name
  const char *name() const;
};

extern AppConfig cfg;

#endif
