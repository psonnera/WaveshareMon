/*
  LibreLinkUpClient.cpp - LibreLinkUp (LibreView follower) API polling
  (part of WaveshareMon, GPL v3, see LICENSE)

  Endpoint usage, headers and the Account-Id derivation follow
  nightscout-librelink-up (Timo Schlueter, MIT) and xdripswift (Johan
  Degraeve, MIT).

  Copyright (C) 2026 Patrick Sonnerat
*/
#include <esp_attr.h>
#include "LibreLinkUpClient.h"
#include "AppConfig.h"
#include "GlucoseState.h"
#include "WifiService.h"
#include "HttpsClient.h"
#include "SessionCache.h"
#include "TimeService.h"
#include "Log.h"
#include <ArduinoJson.h>
#include <mbedtls/sha256.h>

#define LL_BACKOFF_S   (15 * 60)
#define LL_LOCK_S      (5 * 60)

static uint32_t s_nextFetchMs = 0;
static bool     s_force = false;
static char     s_status[32] = "";
static LibreSession *s_sess = nullptr;   // large (JWT): allocated on first use
static bool     s_sessLoaded = false;
RTC_DATA_ATTR static int64_t s_blockedUntil = 0;

static void setStatus(const char *s) { strlcpy(s_status, s, sizeof(s_status)); }

static void hostFor(char *out, size_t len) {
  const char *r = s_sess->region[0] ? s_sess->region : cfg.llRegion;
  if (!r[0]) snprintf(out, len, "api.libreview.io");
  else if (strcmp(r, "ru") == 0) snprintf(out, len, "api.libreview.ru");
  else snprintf(out, len, "api-%s.libreview.io", r);
}

static int request(const char *method, const char *path, const char *body, bool auth, String &resp) {
  char host[48];
  hostFor(host, sizeof(host));
  String url = String("https://") + host + path;
  String bearer = auth ? String("Bearer ") + s_sess->token : String();
  HttpHeader h[8];
  int n = 0;
  h[n++] = {"product", "llu.ios"};
  h[n++] = {"version", cfg.llVersion};
  h[n++] = {"Content-Type", "application/json;charset=UTF-8"};
  h[n++] = {"Accept", "application/json"};
  h[n++] = {"User-Agent", "Mozilla/5.0 (iPhone; CPU OS 17_4.1 like Mac OS X) AppleWebKit/536.26 (KHTML, like Gecko) Version/17.4.1 Mobile/10A5355d Safari/8536.25"};
  if (auth) {
    h[n++] = {"Authorization", bearer.c_str()};
    h[n++] = {"Account-Id", s_sess->accountIdHash};
  }
  return httpsRequest(method, url.c_str(), body, h, n, resp);
}

static void sha256hex(const char *in, char *out) {
  unsigned char d[32];
  mbedtls_sha256_ret((const unsigned char *)in, strlen(in), d, 0);
  for (int i = 0; i < 32; i++) sprintf(out + 2 * i, "%02x", d[i]);
  out[64] = 0;
}

// interpret a login-style response; returns true when a usable token is stored
static bool takeTicket(JsonDocument &d) {
  const char *tok = d["data"]["authTicket"]["token"] | (const char *)nullptr;
  const char *uid = d["data"]["user"]["id"] | (const char *)nullptr;
  if (!tok || !uid) return false;
  strlcpy(s_sess->token, tok, sizeof(s_sess->token));
  s_sess->expires = d["data"]["authTicket"]["expires"] | (int64_t)0;
  sha256hex(uid, s_sess->accountIdHash);
  return true;
}

static bool login() {
  time_t now = time(nullptr);
  if (s_blockedUntil && now < (time_t)s_blockedUntil) { setStatus("Libre: login blocked"); return false; }
  JsonDocument d;
  String body, resp;
  d["email"] = cfg.llUser;
  d["password"] = cfg.llPass;
  serializeJson(d, body);
  for (int attempt = 0; attempt < 3; attempt++) {
    int code = request("POST", "/llu/auth/login", body.c_str(), false, resp);
    d.clear();
    bool parsed = deserializeJson(d, resp) == DeserializationError::Ok;
    int status = parsed ? (d["status"] | -1) : -1;
    if (code == 200 && status == 0) {
      if (d["data"]["redirect"] | false) {
        const char *region = d["data"]["region"] | "";
        strlcpy(s_sess->region, region, sizeof(s_sess->region));
        logAdd("Libre: region %s", region);
        continue;                                  // log in at the regional host
      }
      if (!takeTicket(d)) { setStatus("Libre: login error"); return false; }
      sessSaveLibre(*s_sess);
      logAdd("Libre: logged in");
      return true;
    }
    if (code == 200 && status == 4) {
      // terms of use / privacy policy must be accepted with the short-lived token
      const char *type = d["data"]["step"]["type"] | "tou";
      if (!takeTicket(d)) { setStatus("Libre: accept terms"); return false; }
      char path[48];
      snprintf(path, sizeof(path), "/auth/continue/%s", type);
      logAdd("Libre: accepting %s", type);
      int c2 = request("POST", path, "", true, resp);
      d.clear();
      if (c2 == 200 && !deserializeJson(d, resp) && (d["status"] | -1) == 0 && takeTicket(d)) {
        sessSaveLibre(*s_sess);
        logAdd("Libre: logged in");
        return true;
      }
      continue;                                    // a second step (pp) may follow
    }
    if (code == 200 && status == 2) {
      logAdd("Libre: bad credentials");
      s_blockedUntil = now + LL_BACKOFF_S;
      setStatus("Libre: bad login");
      return false;
    }
    if (code == 403 && status == 920) {
      const char *minv = d["data"]["minimumVersion"] | "?";
      logAdd("Libre: needs version %s", minv);
      char b[32]; snprintf(b, sizeof(b), "Libre: set ver %s", minv); setStatus(b);
      s_blockedUntil = now + 3600;
      return false;
    }
    if (code == 429) {
      long lock = d["data"]["data"]["lockout"] | (long)LL_LOCK_S;
      logAdd("Libre: locked %lds", lock);
      s_blockedUntil = now + lock;
      setStatus("Libre: locked out");
      return false;
    }
    logAdd("Libre login: HTTP %d status %d", code, status);
    if (code < 0) setStatus("Libre: no reply");
    else { char b[32]; snprintf(b, sizeof(b), "Libre: HTTP %d", code); setStatus(b); }
    s_blockedUntil = now + 60;
    return false;
  }
  setStatus("Libre: login error");
  return false;
}

// "5/21/2022 1:38:50 PM" (UTC) -> epoch
static time_t parseFactoryTs(const char *s) {
  if (!s) return 0;
  int M, D, Y, h, m, sec; char ap[3] = "";
  if (sscanf(s, "%d/%d/%d %d:%d:%d %2s", &M, &D, &Y, &h, &m, &sec, ap) < 6) return 0;
  if (ap[0] == 'P' && h < 12) h += 12;
  if (ap[0] == 'A' && h == 12) h = 0;
  struct tm t = {};
  t.tm_year = Y - 1900; t.tm_mon = M - 1; t.tm_mday = D;
  t.tm_hour = h; t.tm_min = m; t.tm_sec = sec;
  return TimeService::utcFromTm(t);
}

static int arrowToAngle(int a) {
  switch (a) {
    case 1: return 75;   case 2: return 45;  case 3: return 0;
    case 4: return -45;  case 5: return -75; default: return ARROW_HIDDEN;
  }
}

// returns 1 ok, 0 token rejected (login again), -1 other failure
static int fetch() {
  String resp;
  if (!s_sess->patientId[0]) {
    int code = request("GET", "/llu/connections", nullptr, true, resp);
    if (code == 401) return 0;
    JsonDocument d;
    if (code != 200 || deserializeJson(d, resp) || (d["status"] | -1) != 0) {
      logAdd("Libre connections: HTTP %d", code);
      setStatus(code < 0 ? "Libre: no reply" : "Libre: connections");
      return -1;
    }
    JsonArray arr = d["data"].as<JsonArray>();
    if (arr.isNull() || arr.size() == 0) { logAdd("Libre: no patient shares data"); setStatus("Libre: no patient"); return -1; }
    const char *pid = arr[0]["patientId"] | "";
    strlcpy(s_sess->patientId, pid, sizeof(s_sess->patientId));
    sessSaveLibre(*s_sess);
    logAdd("Libre: patient %.8s...", pid);
  }
  char path[96];
  snprintf(path, sizeof(path), "/llu/connections/%s/graph", s_sess->patientId);
  int code = request("GET", path, nullptr, true, resp);
  if (code == 401) return 0;
  if (code != 200) {
    logAdd("Libre graph: HTTP %d", code);
    if (code < 0) setStatus("Libre: no reply");
    else if (code == 429) { s_blockedUntil = time(nullptr) + LL_LOCK_S; setStatus("Libre: rate limited"); }
    else { char b[32]; snprintf(b, sizeof(b), "Libre: HTTP %d", code); setStatus(b); }
    return -1;
  }
  // only the fields used, the full response is tens of kB
  JsonDocument filter;
  filter["status"] = true;
  filter["data"]["connection"]["glucoseMeasurement"]["ValueInMgPerDl"] = true;
  filter["data"]["connection"]["glucoseMeasurement"]["FactoryTimestamp"] = true;
  filter["data"]["connection"]["glucoseMeasurement"]["TrendArrow"] = true;
  filter["data"]["graphData"][0]["ValueInMgPerDl"] = true;
  filter["data"]["graphData"][0]["FactoryTimestamp"] = true;
  JsonDocument d;
  if (deserializeJson(d, resp, DeserializationOption::Filter(filter)) || (d["status"] | -1) != 0) {
    logAdd("Libre: bad JSON");
    setStatus("Libre: bad data");
    return -1;
  }
  JsonObject gm = d["data"]["connection"]["glucoseMeasurement"].as<JsonObject>();
  int sgv = gm["ValueInMgPerDl"] | 0;
  time_t utc = parseFactoryTs(gm["FactoryTimestamp"] | (const char *)nullptr);
  if (sgv < 10) { setStatus("Libre: no value"); return -1; }
  bool isNew = !(gs.hasData && gs.mgdl == (uint16_t)sgv && gs.readingUtc == utc);
  gs.onReading((uint16_t)sgv, utc, arrowToAngle(gm["TrendArrow"] | 0));
  if (isNew) {
    // graphData is ~15-minute spaced: place each point in its 5-minute slot
    // of the last four hours and forward-fill the gaps
    uint16_t hist[HIST_SIZE] = {0};
    time_t t0 = utc - (HIST_SIZE - 1) * 300;
    for (JsonObject p : d["data"]["graphData"].as<JsonArray>()) {
      int v = p["ValueInMgPerDl"] | 0;
      time_t pt = parseFactoryTs(p["FactoryTimestamp"] | (const char *)nullptr);
      if (v < 10 || !pt || pt < t0) continue;
      int slot = (int)((pt - t0 + 150) / 300);
      if (slot >= 0 && slot < HIST_SIZE) hist[slot] = (uint16_t)v;
    }
    hist[HIST_SIZE - 1] = (uint16_t)sgv;
    int firstIdx = -1;
    for (int i = 0; i < HIST_SIZE; i++) {
      if (hist[i]) { if (firstIdx < 0) firstIdx = i; }
      else if (firstIdx >= 0) hist[i] = hist[i - 1];
    }
    if (firstIdx >= 0) gs.replaceHistory(hist + firstIdx, (uint8_t)(HIST_SIZE - firstIdx));
    logAdd("Libre: %d", sgv);
  }
  setStatus("");
  return 1;
}

void llRequestNow() { s_force = true; }
const char *llStatus() { return s_status; }

void llForgetSession() {
  if (s_sess) memset(s_sess, 0, sizeof(*s_sess));
  sessClearLibre();
  s_blockedUntil = 0;
  s_sessLoaded = true;
}

void llTick() {
  if (cfg.source != SRC_LIBRE || !cfg.llConfigured() || !wifiConnected()) return;
  uint32_t now = millis();
  if (!s_force && s_nextFetchMs && (int32_t)(now - s_nextFetchMs) < 0) return;
  s_force = false;
  if (!s_sess) s_sess = new LibreSession();
  if (!s_sessLoaded) { sessLoadLibre(*s_sess); s_sessLoaded = true; }

  int r = -1;
  time_t t = time(nullptr);
  bool expired = s_sess->expires && t > (time_t)s_sess->expires - 86400;
  if (s_sess->token[0] && !expired) r = fetch();
  if (r == 0 || !s_sess->token[0] || expired) {
    s_sess->token[0] = 0;
    if (login()) r = fetch();
    if (r == 0) { logAdd("Libre: token rejected twice"); setStatus("Libre: session"); }
  }

  uint32_t wait = r == 1 ? 60000 : 15000;
  if (r == 1 && gs.hasData && gs.readingUtc) {
    long due = (long)difftime(gs.readingUtc + 315, time(nullptr));
    if (due > 20 && due < 400) wait = (uint32_t)due * 1000;
  }
  if (s_blockedUntil && time(nullptr) < (time_t)s_blockedUntil) wait = 5UL * 60000;
  s_nextFetchMs = now + wait;
}
