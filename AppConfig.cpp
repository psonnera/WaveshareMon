/*
  AppConfig.cpp - persisted settings (NVS Preferences)
  (part of WaveshareMon, GPL v3, see LICENSE; derived from M5Stack_xDripMon)

  Copyright (C) 2026 Patrick Sonnerat
*/
#include "AppConfig.h"
#include "Log.h"
#include <Preferences.h>
#include <esp_system.h>

AppConfig cfg;

static const char *NVS_NS = "wsmon";
// bump when the stored layout changes incompatibly
static const uint16_t CFG_VERSION = 1;

static char s_autoName[24] = "";

const char *AppConfig::name() const {
  if (deviceName[0]) return deviceName;
  if (!s_autoName[0]) {
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    snprintf(s_autoName, sizeof(s_autoName), "WaveshareMon-%02X%02X", mac[4], mac[5]);
  }
  return s_autoName;
}

void AppConfig::load() {
  Preferences p;
  p.begin(NVS_NS, true);
  if (p.getUShort("ver", 0) != CFG_VERSION) {
    firstRun = true;
  } else {
    source         = p.getUChar("src", source);
    units          = p.getUChar("units", units);
    yellowLow      = p.getUShort("ylo", yellowLow);
    yellowHigh     = p.getUShort("yhi", yellowHigh);
    redLow         = p.getUShort("rlo", redLow);
    redHigh        = p.getUShort("rhi", redHigh);
    alarmsEnabled  = p.getUChar("aen", alarmsEnabled);
    warnLow        = p.getUShort("swlo", warnLow);
    alarmLow       = p.getUShort("salo", alarmLow);
    warnHigh       = p.getUShort("swhi", warnHigh);
    alarmHigh      = p.getUShort("sahi", alarmHigh);
    noReadingsMin  = p.getUShort("snor", noReadingsMin);
    warnVolume     = p.getUChar("wvol", warnVolume);
    alarmVolume    = p.getUChar("avol", alarmVolume);
    alarmRepeatMin = p.getUChar("arep", alarmRepeatMin);
    snoozeMin      = p.getUChar("snoz", snoozeMin);
    timeFormat24   = p.getUChar("tfmt", timeFormat24);
    dateFormatDMY  = p.getUChar("dfmt", dateFormatDMY);
    debugLog       = p.getUChar("dbg", debugLog);
    noSleep        = p.getUChar("nosleep", noSleep);
    tzOffsetSec    = p.getInt("tzof", tzOffsetSec);
    p.getString("tzstr", tzString, sizeof(tzString));
    p.getString("ssid", wifiSsid, sizeof(wifiSsid));
    p.getString("pass", wifiPass, sizeof(wifiPass));
    p.getString("nsurl", nsUrl, sizeof(nsUrl));
    p.getString("nstok", nsToken, sizeof(nsToken));
    p.getString("dxusr", dxUser, sizeof(dxUser));
    p.getString("dxpwd", dxPass, sizeof(dxPass));
    dxRegion       = p.getUChar("dxreg", dxRegion);
    p.getString("llusr", llUser, sizeof(llUser));
    p.getString("llpwd", llPass, sizeof(llPass));
    p.getString("llreg", llRegion, sizeof(llRegion));
    p.getString("llver", llVersion, sizeof(llVersion));
    if (!llVersion[0]) strlcpy(llVersion, "4.16.0", sizeof(llVersion));
    tlsVerify      = p.getUChar("tlsv", tlsVerify);
    obbStatusLine  = p.getUChar("sline", obbStatusLine);
    bleSecureConn  = p.getUChar("blesc", bleSecureConn);
    p.getString("name", deviceName, sizeof(deviceName));
    p.getString("x4ipw", x4iPassword, sizeof(x4iPassword));
    mibandKeySet   = p.getUChar("mbset", 0);
    if (p.getBytesLength("mbkey") == sizeof(mibandKey)) p.getBytes("mbkey", mibandKey, sizeof(mibandKey));
    else mibandKeySet = 0;
  }
  p.end();
}

// putString returns 0 for "" even on success, so strings are not counted
static void putStr(Preferences &p, const char *key, const char *val) {
  if (val[0]) p.putString(key, val); else p.remove(key);
}

void AppConfig::save() {
  Preferences p;
  p.begin(NVS_NS, false);
  int failed = 0;
  auto chk = [&failed](size_t written) { if (written == 0) failed++; };
  chk(p.putUShort("ver", CFG_VERSION));
  chk(p.putUChar("src", source));
  chk(p.putUChar("units", units));
  chk(p.putUShort("ylo", yellowLow));
  chk(p.putUShort("yhi", yellowHigh));
  chk(p.putUShort("rlo", redLow));
  chk(p.putUShort("rhi", redHigh));
  chk(p.putUChar("aen", alarmsEnabled));
  chk(p.putUShort("swlo", warnLow));
  chk(p.putUShort("salo", alarmLow));
  chk(p.putUShort("swhi", warnHigh));
  chk(p.putUShort("sahi", alarmHigh));
  chk(p.putUShort("snor", noReadingsMin));
  chk(p.putUChar("wvol", warnVolume));
  chk(p.putUChar("avol", alarmVolume));
  chk(p.putUChar("arep", alarmRepeatMin));
  chk(p.putUChar("snoz", snoozeMin));
  chk(p.putUChar("tfmt", timeFormat24));
  chk(p.putUChar("dfmt", dateFormatDMY));
  chk(p.putUChar("dbg", debugLog));
  chk(p.putUChar("nosleep", noSleep));
  chk(p.putInt("tzof", tzOffsetSec));
  chk(p.putUChar("sline", obbStatusLine));
  chk(p.putUChar("blesc", bleSecureConn));
  putStr(p, "tzstr", tzString);
  putStr(p, "ssid", wifiSsid);
  putStr(p, "pass", wifiPass);
  putStr(p, "nsurl", nsUrl);
  putStr(p, "nstok", nsToken);
  putStr(p, "dxusr", dxUser);
  putStr(p, "dxpwd", dxPass);
  chk(p.putUChar("dxreg", dxRegion));
  putStr(p, "llusr", llUser);
  putStr(p, "llpwd", llPass);
  putStr(p, "llreg", llRegion);
  putStr(p, "llver", llVersion);
  chk(p.putUChar("tlsv", tlsVerify));
  putStr(p, "name", deviceName);
  putStr(p, "x4ipw", x4iPassword);
  chk(p.putUChar("mbset", mibandKeySet));
  if (mibandKeySet) chk(p.putBytes("mbkey", mibandKey, sizeof(mibandKey))); else p.remove("mbkey");
  p.end();
  if (failed)
    logAdd("config save FAILED (%d keys)", failed);
  else
    logDebug("cfg saved");
}

void AppConfig::factoryReset() {
  Preferences p;
  p.begin(NVS_NS, false);
  p.clear();
  p.end();
  *this = AppConfig{};
  firstRun = true;
}

// after the first configuration write: the device is set up, the power cycle may start
void AppConfig::markConfigured() { firstRun = false; }
