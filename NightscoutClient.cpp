/*
  NightscoutClient.cpp - Nightscout REST polling
  (part of WaveshareMon, GPL v3, see LICENSE)

  Endpoint usage and JSON hygiene follow readNightscout() of M5_NightscoutMon,
  Copyright (C) Martin Lukasek <martin@lukasek.cz>, GPL v3.

  Copyright (C) 2026 Patrick Sonnerat
*/
#include "NightscoutClient.h"
#include "AppConfig.h"
#include "GlucoseState.h"
#include "WifiService.h"
#include "HttpsClient.h"
#include "Log.h"
#include <ArduinoJson.h>
#define HTTP_CODE_OK 200

static uint32_t s_lastFetchMs = 0;
static uint32_t s_nextFetchMs = 0;
static int      s_lastErr = 0;
static bool     s_force = false;

#define NS_ENTRIES 48

static void buildUrl(char *out, size_t len, const char *path) {
  if (strncmp(cfg.nsUrl, "http", 4) != 0) strlcpy(out, "https://", len);
  else out[0] = 0;
  strlcat(out, cfg.nsUrl, len);
  size_t n = strlen(out);
  while (n && out[n - 1] == '/') out[--n] = 0;
  strlcat(out, path, len);
  if (cfg.nsToken[0]) {
    strlcat(out, strchr(path, '?') ? "&token=" : "?token=", len);
    strlcat(out, cfg.nsToken, len);
  }
}

// GET url into body; returns HTTP code (or negative HTTPClient error)
static int httpGet(const char *url, String &body) {
  static const HttpHeader h[] = {{"Accept", "application/json"}};
  return httpsRequest("GET", url, nullptr, h, 1, body);
}

static void cleanJson(String &json) {
  // control characters and unicode surrogates upset ArduinoJson (Ascensia meters, Medtronic)
  for (unsigned i = 0; i < json.length(); i++)
    if ((uint8_t)json.charAt(i) < 32) json.setCharAt(i, ' ');
  json.replace("\\u0000", " ");
  json.replace("\\u000b", " ");
  // CGMBLEKit sends fractional milliseconds in "date"; keep integers only
  int sr = json.indexOf("\"date\":");
  while (sr != -1) {
    int p = sr + 7;
    while (p < (int)json.length() && json.charAt(p) >= '0' && json.charAt(p) <= '9') p++;
    if (p < (int)json.length() && json.charAt(p) == '.') {
      int e = p;
      while (e < (int)json.length() && (json.charAt(e) == '.' || (json.charAt(e) >= '0' && json.charAt(e) <= '9'))) e++;
      json.remove(p, e - p);
    }
    sr = json.indexOf("\"date\":", p);
  }
}

static bool fetchEntries() {
  char url[256];
  char path[64];
  snprintf(path, sizeof(path), "/api/v1/entries.json?find[type][$eq]=sgv&count=%d", NS_ENTRIES);
  buildUrl(url, sizeof(url), path);
  String body;
  int code = httpGet(url, body);
  if (code != HTTP_CODE_OK) {
    s_lastErr = code;
    logAdd("NS entries: HTTP %d", code);
    return false;
  }
  cleanJson(body);
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  JsonArray arr = doc.as<JsonArray>();
  if (err || arr.isNull() || arr.size() == 0) {
    s_lastErr = err ? 1001 : 1002;
    logAdd("NS entries: %s", err ? "bad JSON" : "no data");
    return false;
  }
  // newest first in Nightscout; collect oldest-first for the history
  uint16_t hist[NS_ENTRIES];
  int n = 0;
  for (int i = (int)arr.size() - 1; i >= 0 && n < NS_ENTRIES; i--) {
    int sgv = arr[i]["sgv"] | 0;
    if (sgv >= 10 && sgv <= 600) hist[n++] = (uint16_t)sgv;
  }
  JsonObject first = arr[0].as<JsonObject>();
  int sgv = first["sgv"] | 0;
  long long dateMs = first["date"].as<long long>();
  const char *dir = first["direction"] | first["trend"] | "N/A";
  if (sgv < 10) { s_lastErr = 1004; logAdd("NS: no sgv"); return false; }

  time_t utc = (time_t)(dateMs / 1000);
  int16_t delta = 0; bool deltaValid = false;
  if (arr.size() >= 2) {
    int prev = arr[1]["sgv"] | 0;
    long long prevMs = arr[1]["date"].as<long long>();
    long gap = (long)((dateMs - prevMs) / 1000);
    if (prev >= 10 && gap > 30 && gap < 450) { delta = (int16_t)(sgv - prev); deltaValid = true; }
  }
  bool isNew = !(gs.hasData && gs.mgdl == (uint16_t)sgv && gs.readingUtc == utc);
  gs.onReading((uint16_t)sgv, utc, nsDirectionToAngle(dir));
  if (isNew) {
    if (n > 1) gs.replaceHistory(hist, (uint8_t)n);
    gs.setDelta(delta, deltaValid);
    logAdd("NS: %d %s", sgv, dir);
  }
  s_lastErr = 0;
  return true;
}

// a Nightscout "display" value as text: string as is, number formatted, else nullptr
static const char *displayOf(JsonVariantConst v, char *buf, size_t len) {
  if (v.is<const char *>()) return v.as<const char *>();
  if (v.is<float>()) {
    float f = v.as<float>();
    snprintf(buf, len, (f == (long)f) ? "%.0f" : "%.1f", f);
    return buf;
  }
  return nullptr;
}

static void fetchProperties() {
  char url[256];
  buildUrl(url, sizeof(url), "/api/v2/properties/iob,cob,basal");
  String body;
  if (httpGet(url, body) != HTTP_CODE_OK) return;
  JsonDocument doc;
  if (deserializeJson(doc, body)) return;
  char line[96] = "";
  // "display" is a string for iob/basal but a bare number for cob
  char iobBuf[16], cobBuf[16], basBuf[16];
  const char *iob = displayOf(doc["iob"]["display"], iobBuf, sizeof(iobBuf));
  const char *cob = displayOf(doc["cob"]["display"], cobBuf, sizeof(cobBuf));
  const char *bas = displayOf(doc["basal"]["display"], basBuf, sizeof(basBuf));
  if (iob) { strlcat(line, "IOB ", sizeof(line)); strlcat(line, iob, sizeof(line)); }
  if (cob) { strlcat(line, iob ? "  COB " : "COB ", sizeof(line)); strlcat(line, cob, sizeof(line)); }
  if (bas && !cob) { strlcat(line, line[0] ? "  " : "", sizeof(line)); strlcat(line, bas, sizeof(line)); }
  gs.setInfoLine(line);
}

void nsRequestNow() { s_force = true; }
int nsLastError() { return s_lastErr; }
uint32_t nsLastFetchMs() { return s_lastFetchMs; }

void nsTick() {
  if (cfg.source != SRC_NIGHTSCOUT || !cfg.nsConfigured() || !wifiConnected()) return;
  uint32_t now = millis();
  if (!s_force && s_nextFetchMs && (int32_t)(now - s_nextFetchMs) < 0) return;
  s_force = false;
  s_lastFetchMs = now;
  bool ok = fetchEntries();
  if (ok) fetchProperties();

  // next poll: 15 s after the expected next reading, else retry soon
  uint32_t wait = ok ? 60000 : 15000;
  if (ok && gs.hasData && gs.readingUtc) {
    time_t t = time(nullptr);
    long due = (long)difftime(gs.readingUtc + 315, t);
    if (due > 20 && due < 400) wait = (uint32_t)due * 1000;
  }
  s_nextFetchMs = now + wait;
}
