/*
  DexcomShareClient.cpp - Dexcom Share follower API polling
  (part of WaveshareMon, GPL v3, see LICENSE)

  Endpoint usage follows pydexcom (Gage Benne, MIT) and share2nightscout-
  bridge (Nightscout, MIT).

  Copyright (C) 2026 Patrick Sonnerat
*/
#include <esp_attr.h>
#include "DexcomShareClient.h"
#include "AppConfig.h"
#include "GlucoseState.h"
#include "WifiService.h"
#include "HttpsClient.h"
#include "SessionCache.h"
#include "Log.h"
#include <ArduinoJson.h>

#define DX_APP_ID     "d89443d2-327c-4a6f-89e5-496bbb0317db"
#define DX_APP_ID_JP  "d8665ade-9673-4e27-9ff6-92db4ce13d13"
#define DX_NULL_UUID  "00000000-0000-0000-0000-000000000000"
#define DX_BACKOFF_S  (15 * 60)

static uint32_t s_nextFetchMs = 0;
static bool     s_force = false;
static char     s_status[32] = "";
static DexcomSession s_sess;
static bool     s_sessLoaded = false;
// credential failures: no login attempts before this (kept across deep sleep)
RTC_DATA_ATTR static int64_t s_blockedUntil = 0;

static const char *host() {
  switch (cfg.dxRegion) {
    case DX_REGION_US: return "share2.dexcom.com";
    case DX_REGION_JP: return "share.dexcom.jp";
    default:           return "shareous1.dexcom.com";
  }
}
static const char *appId() { return cfg.dxRegion == DX_REGION_JP ? DX_APP_ID_JP : DX_APP_ID; }

static const HttpHeader HEADERS[] = {
  {"Content-Type", "application/json"},
  {"Accept", "application/json"},
  {"User-Agent", "Dexcom Share/3.0.2.11 CFNetwork/711.2.23 Darwin/14.0.0"},
};
#define NHEADERS (sizeof(HEADERS) / sizeof(HEADERS[0]))

static void setStatus(const char *s) { strlcpy(s_status, s, sizeof(s_status)); }

// POST helper returning the HTTP code; body may be an empty object
static int post(const char *path, const String &body, String &resp) {
  String url = String("https://") + host() + "/ShareWebServices/Services/" + path;
  return httpsRequest("POST", url.c_str(), body.c_str(), HEADERS, NHEADERS, resp);
}

// error JSON: {"Code":"SessionNotValid", ...}
static String errorCode(const String &resp) {
  JsonDocument d;
  if (deserializeJson(d, resp)) return "";
  const char *c = d["Code"] | "";
  return String(c);
}

// a JSON string ("...") or an object with accountId: strip to the bare value
static bool extractId(const String &resp, const char *key, char *out, size_t len) {
  String t = resp;
  t.trim();
  if (t.startsWith("\"") && t.endsWith("\"") && t.length() > 2) {
    strlcpy(out, t.substring(1, t.length() - 1).c_str(), len);
    return true;
  }
  JsonDocument d;
  if (deserializeJson(d, t)) return false;
  const char *v = d[key] | "";
  if (!v[0]) return false;
  strlcpy(out, v, len);
  return true;
}

static bool credentialError(const String &code) {
  return code == "AccountPasswordInvalid" || code.startsWith("SSO_") || code == "InvalidArgument";
}

static bool login() {
  time_t now = time(nullptr);
  if (s_blockedUntil && now < (time_t)s_blockedUntil) {
    setStatus("Dexcom: login blocked");
    return false;
  }
  JsonDocument d;
  String body, resp;
  if (!s_sess.accountId[0]) {
    d["accountName"] = cfg.dxUser;
    d["password"] = cfg.dxPass;
    d["applicationId"] = appId();
    serializeJson(d, body);
    int code = post("General/AuthenticatePublisherAccount", body, resp);
    if (code != 200 || !extractId(resp, "accountId", s_sess.accountId, sizeof(s_sess.accountId))) {
      String ec = errorCode(resp);
      logAdd("Dexcom auth: HTTP %d %s", code, ec.c_str());
      if (credentialError(ec) || code == 500) { s_blockedUntil = now + DX_BACKOFF_S; setStatus("Dexcom: bad login"); }
      else setStatus(code < 0 ? "Dexcom: no reply" : "Dexcom: auth error");
      return false;
    }
  }
  d.clear();
  d["accountId"] = s_sess.accountId;
  d["password"] = cfg.dxPass;
  d["applicationId"] = appId();
  body = "";
  serializeJson(d, body);
  int code = post("General/LoginPublisherAccountById", body, resp);
  char sid[40] = "";
  if (code != 200 || !extractId(resp, "sessionId", sid, sizeof(sid)) || strcmp(sid, DX_NULL_UUID) == 0) {
    String ec = errorCode(resp);
    logAdd("Dexcom login: HTTP %d %s", code, ec.c_str());
    s_sess.accountId[0] = 0;               // start over next time
    if (credentialError(ec) || code == 500) { s_blockedUntil = now + DX_BACKOFF_S; setStatus("Dexcom: bad login"); }
    else setStatus(code < 0 ? "Dexcom: no reply" : "Dexcom: login error");
    sessSaveDexcom(s_sess);
    return false;
  }
  strlcpy(s_sess.sessionId, sid, sizeof(s_sess.sessionId));
  sessSaveDexcom(s_sess);
  logAdd("Dexcom: logged in");
  return true;
}

// "Date(1690000000000)" or "/Date(1690000000000-0700)/" -> epoch seconds
static time_t parseDate(const char *s) {
  if (!s) return 0;
  const char *p = strstr(s, "Date(");
  if (!p) return 0;
  p += 5;
  long long ms = 0;
  while (*p >= '0' && *p <= '9') { ms = ms * 10 + (*p - '0'); p++; }
  return (time_t)(ms / 1000);
}

static int trendToAngle(JsonVariant t) {
  if (t.is<const char *>()) {
    const char *n = t.as<const char *>();
    int a = nsDirectionToAngle(n);
    return a;                           // NotComputable / RateOutOfRange -> hidden
  }
  switch (t.as<int>()) {                // legacy numeric trend
    case 1: return -90; case 2: return -75; case 3: return -45; case 4: return 0;
    case 5: return 45;  case 6: return 75;  case 7: return 90;
    default: return ARROW_HIDDEN;
  }
}

// returns: 1 ok, 0 session error (login and retry), -1 other failure
static int fetch() {
  int maxCount = gs.histCount < 40 ? 48 : 3;
  char path[128];
  snprintf(path, sizeof(path), "Publisher/ReadPublisherLatestGlucoseValues?sessionId=%s&minutes=1440&maxCount=%d",
           s_sess.sessionId, maxCount);
  String resp;
  int code = post(path, "{}", resp);
  if (code != 200) {
    String ec = errorCode(resp);
    if (ec == "SessionIdNotFound" || ec == "SessionNotValid") return 0;
    logAdd("Dexcom: HTTP %d %s", code, ec.c_str());
    if (code < 0) setStatus("Dexcom: no reply");
    else { char b[32]; snprintf(b, sizeof(b), "Dexcom: HTTP %d", code); setStatus(b); }
    return -1;
  }
  JsonDocument doc;
  if (deserializeJson(doc, resp) || !doc.is<JsonArray>() || doc.as<JsonArray>().size() == 0) {
    logAdd("Dexcom: no data");
    setStatus("Dexcom: no data");
    return -1;
  }
  JsonArray arr = doc.as<JsonArray>();
  // newest first; collect oldest-first for the history
  uint16_t hist[48];
  int n = 0;
  for (int i = (int)arr.size() - 1; i >= 0 && n < 48; i--) {
    int v = arr[i]["Value"] | 0;
    if (v >= 10 && v <= 600) hist[n++] = (uint16_t)v;
  }
  JsonObject first = arr[0].as<JsonObject>();
  int sgv = first["Value"] | 0;
  time_t utc = parseDate(first["WT"] | (const char *)nullptr);
  if (sgv < 10) { setStatus("Dexcom: no value"); return -1; }
  int16_t delta = 0; bool deltaValid = false;
  if (arr.size() >= 2) {
    int prev = arr[1]["Value"] | 0;
    time_t prevUtc = parseDate(arr[1]["WT"] | (const char *)nullptr);
    long gap = (long)difftime(utc, prevUtc);
    if (prev >= 10 && gap > 30 && gap < 450) { delta = (int16_t)(sgv - prev); deltaValid = true; }
  }
  bool isNew = !(gs.hasData && gs.mgdl == (uint16_t)sgv && gs.readingUtc == utc);
  gs.onReading((uint16_t)sgv, utc, trendToAngle(first["Trend"]));
  if (isNew) {
    if (n > 1) gs.replaceHistory(hist, (uint8_t)n);
    gs.setDelta(delta, deltaValid);
    logAdd("Dexcom: %d", sgv);
  }
  setStatus("");
  return 1;
}

void dxRequestNow() { s_force = true; }
const char *dxStatus() { return s_status; }

void dxForgetSession() {
  memset(&s_sess, 0, sizeof(s_sess));
  sessClearDexcom();
  s_blockedUntil = 0;
  s_sessLoaded = true;
}

void dxTick() {
  if (cfg.source != SRC_DEXCOM || !cfg.dxConfigured() || !wifiConnected()) return;
  uint32_t now = millis();
  if (!s_force && s_nextFetchMs && (int32_t)(now - s_nextFetchMs) < 0) return;
  s_force = false;
  if (!s_sessLoaded) { sessLoadDexcom(s_sess); s_sessLoaded = true; }

  int r = -1;
  if (s_sess.sessionId[0]) r = fetch();
  if (r == 0 || !s_sess.sessionId[0]) {
    s_sess.sessionId[0] = 0;
    if (login()) r = fetch();
    if (r == 0) { logAdd("Dexcom: session rejected twice"); setStatus("Dexcom: session"); }
  }

  // next poll: 15 s after the expected next reading, else retry soon
  uint32_t wait = r == 1 ? 60000 : 15000;
  if (r == 1 && gs.hasData && gs.readingUtc) {
    long due = (long)difftime(gs.readingUtc + 315, time(nullptr));
    if (due > 20 && due < 400) wait = (uint32_t)due * 1000;
  }
  if (s_blockedUntil && time(nullptr) < (time_t)s_blockedUntil) wait = 5UL * 60000;
  s_nextFetchMs = now + wait;
}
