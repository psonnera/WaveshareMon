/*
  AppConfig.h - persisted settings (NVS)
  (part of WaveshareMon, GPL v3, see LICENSE; derived from M5Stack_xDripMon)

  Copyright (C) 2026 Patrick Sonnerat
*/
#ifndef APPCONFIG_H
#define APPCONFIG_H

#include <Arduino.h>

// data sources
#define SRC_OBB        0   // xDrip Open Bluetooth Broadcast (BLE, phone app = server)
#define SRC_NIGHTSCOUT 1   // Nightscout over Wi-Fi
#define SRC_MIBAND     2   // Mi Band 2 emulation (BLE, xDrip pushes directly)
#define SRC_DEXCOM     3   // Dexcom Share over Wi-Fi
#define SRC_LIBRE      4   // LibreLinkUp over Wi-Fi
#define SRC_XDRIP4IOS  5   // xDrip4iOS "M5Stack" protocol (BLE, the iPhone pushes directly)
#define SRC_MAX        5
#define SRC_IS_BLE(s)  ((s) == SRC_OBB || (s) == SRC_MIBAND || (s) == SRC_XDRIP4IOS)
#define SRC_IS_WIFI(s) ((s) == SRC_NIGHTSCOUT || (s) == SRC_DEXCOM || (s) == SRC_LIBRE)

// Dexcom Share regions
#define DX_REGION_US   0
#define DX_REGION_OUS  1
#define DX_REGION_JP   2

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
  uint16_t noReadingsMin   = 15;   // warn when data older than this (value reads "---" from 12)
  uint8_t  warnVolume      = 30;   // 0-100
  uint8_t  alarmVolume     = 80;   // 0-100
  uint8_t  alarmRepeatMin  = 5;
  uint8_t  snoozeMin       = 30;
  // display
  uint8_t  timeFormat24    = 1;    // 1 = 24h, 0 = 12h am/pm
  uint8_t  dateFormatDMY   = 1;    // 1 = d.m., 0 = m/d
  uint8_t  debugLog        = 0;
  uint8_t  noSleep         = 0;    // debug: stay in the always-on loop, never deep-sleep
  // time: POSIX TZ string wins when set, else fixed offset (OBB status / manual)
  char     tzString[48]    = "CET-1CEST,M3.5.0,M10.5.0/3";
  int32_t  tzOffsetSec     = 3600;
  // Wi-Fi + Nightscout
  char     wifiSsid[33]    = "";
  char     wifiPass[64]    = "";
  char     nsUrl[128]      = "";
  char     nsToken[64]     = "";
  // Dexcom Share (the publisher's own account, needs at least one follower)
  char     dxUser[65]      = "";
  char     dxPass[64]      = "";
  uint8_t  dxRegion        = DX_REGION_OUS;
  // LibreLinkUp (a follower account invited from the patient's LibreLink app)
  char     llUser[65]      = "";
  char     llPass[64]      = "";
  char     llRegion[8]     = "";        // "" = find out from the login redirect
  char     llVersion[12]   = "4.16.0";  // the app version Abbott's servers demand
  uint8_t  tlsVerify       = 1;         // 0 = accept any certificate (break-glass)
  // Mi Band 2 emulation: AES key handed over by xDrip on first contact
  uint8_t  mibandKey[16]   = {0};
  uint8_t  mibandKeySet    = 0;
  // xDrip4iOS: 10-character password generated on first contact (empty = none yet)
  char     x4iPassword[12] = "";
  // OBB options
  uint8_t  obbStatusLine   = 0;    // subscribe to the optional status line
  uint8_t  bleSecureConn   = 1;    // LE Secure Connections for bonding (0 = legacy pairing), debug aid
  // mixed into the Bluetooth address (random static). Generated when missing, i.e.
  // after a factory reset or an "Erase device" flash, so the phones see a new
  // device and pair afresh instead of refusing with their stale bond.
  uint32_t bleNonce        = 0;
  // custom name (empty = WaveshareMon-XXXX from the MAC)
  char     deviceName[25]  = "";

  // true after load() when no valid config was found (fresh device / factory
  // reset). Not persisted.
  bool     firstRun        = false;

  void load();
  void save();
  void factoryReset();
  void markConfigured();
  void renewBleAddress();          // new nonce (unbond): the phones see a new device after the reboot

  bool isMgdl() const { return units == UNITS_MGDL; }
  bool wifiConfigured() const { return wifiSsid[0] != 0; }
  bool nsConfigured() const { return nsUrl[0] != 0; }
  bool dxConfigured() const { return dxUser[0] != 0 && dxPass[0] != 0; }
  bool llConfigured() const { return llUser[0] != 0 && llPass[0] != 0; }
  // effective BLE / mDNS name
  const char *name() const;
};

extern AppConfig cfg;

#endif
