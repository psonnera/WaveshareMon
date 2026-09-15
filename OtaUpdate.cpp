/*
  OtaUpdate.cpp - firmware update over Wi-Fi from the GitHub repository
  (part of WaveshareMon, GPL v3, see LICENSE)

  Copyright (C) 2026 Patrick Sonnerat
*/
#include <esp_attr.h>
#include "OtaUpdate.h"
#include "Version.h"
#include "AppConfig.h"
#include "CaRoots.h"
#include "HttpsClient.h"
#include "WifiService.h"
#include "PowerCycle.h"
#include "Battery.h"
#include "EpdUi.h"
#include "Log.h"
#include <WiFiClientSecure.h>
#include <HTTPUpdate.h>
#include <esp_task_wdt.h>

#define OTA_WAIT_WIFI_MS   60000UL           // give a cold join this long
#define OTA_HOLD_MS        (5UL * 60 * 1000) // stay awake while a request runs
#define OTA_MIN_BATTERY    30                // % needed to install on battery
#define OTA_CHECK_PERIOD_S 86400             // automatic check: once a day

extern EpdUi ui;
extern Battery battery;

static uint8_t  s_req = 0;                   // 0 none, 1 check, 2 install
static uint32_t s_reqMs = 0;
static bool     s_holdingWifi = false;       // we switched Wi-Fi on for the request
static bool     s_busy = false;
static char     s_status[40] = "";

RTC_DATA_ATTR static uint32_t s_latest = 0;        // build on the server, survives deep sleep
RTC_DATA_ATTR static time_t   s_lastCheckUtc = 0;  // last automatic check

static void setStatus(const char *s) { strlcpy(s_status, s, sizeof(s_status)); }

static void fail(const char *why) {
  snprintf(s_status, sizeof(s_status), "failed: %s", why);
  logAdd("update: %s", why);
}

static void release() {
  if (s_holdingWifi) { s_holdingWifi = false; wifiHold(false); }
}

// GET update.inf: a 10-digit build number, YYYYMMDDnn
static bool fetchLatest(char *err, size_t errLen) {
  String body;
  int code = httpsRequest("GET", OTA_BASE_URL "update.inf", nullptr, nullptr, 0, body);
  if (code != 200) {
    if (code < 0) snprintf(err, errLen, "%s", httpsErrorText(code));
    else          snprintf(err, errLen, "server says %d", code);
    return false;
  }
  body.trim();
  if (body.length() != 10) { snprintf(err, errLen, "bad update.inf"); return false; }
  for (unsigned i = 0; i < body.length(); i++)
    if (!isdigit(body[i])) { snprintf(err, errLen, "bad update.inf"); return false; }
  s_latest = (uint32_t)strtoul(body.c_str(), nullptr, 10);
  time_t now = time(nullptr);
  if (now > 1600000000) s_lastCheckUtc = now;
  return true;
}

static void install() {
  logAdd("update: downloading build %lu", (unsigned long)s_latest);
  setStatus("updating 0%");
  cycleStayAwake(OTA_HOLD_MS);

  WiFiClientSecure client;
  if (cfg.tlsVerify) client.setCACert(CA_ROOTS_PEM);
  else               client.setInsecure();

  httpUpdate.rebootOnUpdate(false);
  httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  // the download and the flash writes block loop() for longer than the task
  // watchdog allows: feed it from the progress callback
  static int lastPct;
  lastPct = -1;
  httpUpdate.onProgress([](int done, int total) {
    esp_task_wdt_reset();
    int pct = total > 0 ? (int)((int64_t)done * 100 / total) : 0;
    if (pct / 10 != lastPct / 10) {
      lastPct = pct;
      snprintf(s_status, sizeof(s_status), "updating %d%%", pct);
      Serial.printf("[ota] %d%% (%d / %d)\n", pct, done, total);
    }
  });

  t_httpUpdate_return r = httpUpdate.update(client, String(OTA_BASE_URL "WaveshareMon.ino.bin"));
  switch (r) {
    case HTTP_UPDATE_OK:
      logAdd("update: build %lu installed, rebooting", (unsigned long)s_latest);
      setStatus("installed, rebooting");
      Serial.flush();
      delay(500);
      ESP.restart();
      break;
    case HTTP_UPDATE_NO_UPDATES:
      fail("no image on the server");
      break;
    default: {
      char why[40];
      snprintf(why, sizeof(why), "%s", httpUpdate.getLastErrorString().c_str());
      fail(why[0] ? why : "download failed");
      break;
    }
  }
}

void otaRequest(bool install) {
  if (s_busy) return;
  if (s_req == 2) return;                    // an install is already queued
  s_req = install ? 2 : 1;
  s_reqMs = millis();
  cycleStayAwake(OTA_HOLD_MS);
  if (!wifiConnected()) {
    if (!cfg.wifiConfigured()) { s_req = 0; fail("no Wi-Fi configured"); return; }
    setStatus("waiting for Wi-Fi");
    if (!SRC_IS_WIFI(cfg.source)) { s_holdingWifi = true; wifiHold(true); }
  } else setStatus("checking");
  logAdd("update: %s requested", install ? "install" : "check");
}

bool otaBusy() { return s_busy || s_req != 0; }
const char *otaStatus() { return s_status; }
uint32_t otaLatestBuild() { return s_latest; }

void otaTick() {
  if (s_busy) return;

  if (!s_req) {
    // automatic daily check while Wi-Fi is up for the source anyway: one small
    // GET, the result shows in the app's Info and the log
    time_t now = time(nullptr);
    if (wifiConnected() && now > 1600000000 &&
        (s_lastCheckUtc == 0 || now - s_lastCheckUtc >= OTA_CHECK_PERIOD_S)) {
      s_busy = true;
      char err[40];
      if (fetchLatest(err, sizeof(err))) {
        if (s_latest > WSMON_BUILD) {
          snprintf(s_status, sizeof(s_status), "update %lu available", (unsigned long)s_latest);
          logAdd("update: build %lu available", (unsigned long)s_latest);
        } else setStatus("up to date");
      } else s_lastCheckUtc = now;           // failed: try again tomorrow, not every wake
      s_busy = false;
    }
    return;
  }

  if (!wifiConnected()) {
    if (wifiState() == WS_WIFI_FAILED || millis() - s_reqMs > OTA_WAIT_WIFI_MS) {
      s_req = 0;
      const char *why = wifiFailText();
      fail(why[0] ? why : "no Wi-Fi");
      release();
    }
    return;
  }
  if (ui.busy()) return;                     // not while the panel refreshes

  uint8_t req = s_req;
  s_req = 0;
  s_busy = true;
  setStatus("checking");
  logAdd("update: checking (running build %lu)", (unsigned long)WSMON_BUILD);
  char err[40];
  if (!fetchLatest(err, sizeof(err))) {
    fail(err);
  } else if (s_latest <= WSMON_BUILD) {
    setStatus("up to date");
    logAdd("update: up to date");
  } else {
    snprintf(s_status, sizeof(s_status), "update %lu available", (unsigned long)s_latest);
    logAdd("update: build %lu available", (unsigned long)s_latest);
    if (req == 2) {
      int pct = battery.percent();
      if (!battery.onUsb() && pct >= 0 && pct < OTA_MIN_BATTERY) fail("battery too low, plug USB in");
      else install();                        // does not return when it succeeds
    }
  }
  s_busy = false;
  release();
}
