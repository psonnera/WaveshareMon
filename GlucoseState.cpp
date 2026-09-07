/*
  GlucoseState.cpp - glucose readings, history and staleness tracking
  (part of WaveshareMon, GPL v3, see LICENSE; derived from M5Stack_xDripMon)

  The direction names and angle mapping follow xDrip+ (Nightscout Foundation,
  GPL v3) and M5_NightscoutMon (Martin Lukasek).

  Copyright (C) 2026 Patrick Sonnerat
*/
#include "GlucoseState.h"
#include <Preferences.h>
#include <string.h>

GlucoseState gs;

static const char *NVS_NS = "wsmonhist";

void GlucoseState::onReading(uint16_t newMgdl, time_t utc, int arrowAngleIn) {
  if (newMgdl < 10 || newMgdl > 600) return;   // implausible, ignore

  uint32_t nowMs = millis();
  // de-duplicate: same value and timestamp again (reconnect replays,
  // notify-on-subscribe, Nightscout polls between readings)
  bool sameReading = hasData && newMgdl == mgdl &&
                     ((utc != 0 && utc == readingUtc) ||
                      (utc == 0 && (nowMs - readingMillis) < 120000UL));
  if (sameReading) {
    if (!live) { live = true; dataChanged = true; }
    if (arrowAngleIn != INT32_MIN && arrowAngleIn != arrowAngle) {
      arrowAngle = arrowAngleIn; dataChanged = true;
    }
    return;
  }

  if (hasData) {
    deltaMgdl = (int16_t)newMgdl - (int16_t)mgdl;
    uint32_t gapMs = nowMs - readingMillis;
    deltaValid = gapMs > 30000UL && gapMs < 450000UL;
    if (utc != 0 && readingUtc != 0) {
      long gap = (long)difftime(utc, readingUtc);
      deltaValid = gap > 30 && gap < 450;
    }
  }

  mgdl = newMgdl;
  readingUtc = utc;
  readingMillis = nowMs;
  if (arrowAngleIn != INT32_MIN) arrowAngle = arrowAngleIn;
  hasData = true;
  live = true;
  remoteStale = false;

  if (histCount < HIST_SIZE) {
    hist[histCount++] = newMgdl;
  } else {
    memmove(hist, hist + 1, (HIST_SIZE - 1) * sizeof(hist[0]));
    hist[HIST_SIZE - 1] = newMgdl;
  }

  persist();
  dataChanged = true;
}

void GlucoseState::setDelta(int16_t delta, bool valid) {
  if (deltaValid == valid && deltaMgdl == delta) return;
  deltaMgdl = delta;
  deltaValid = valid;
  dataChanged = true;
}

void GlucoseState::replaceHistory(const uint16_t *vals, uint8_t n) {
  if (n > HIST_SIZE) { vals += n - HIST_SIZE; n = HIST_SIZE; }
  memcpy(hist, vals, n * sizeof(hist[0]));
  histCount = n;
  persist();
  dataChanged = true;
}

void GlucoseState::setInfoLine(const char *s) {
  if (strcmp(infoLine, s) == 0) return;
  strlcpy(infoLine, s, sizeof(infoLine));
  dataChanged = true;
}

int GlucoseState::minutesAgo() const {
  if (!hasData) return 9999;
  time_t now = time(nullptr);
  if (readingUtc != 0 && now > 1600000000) {
    long dif = (long)difftime(now, readingUtc);
    if (dif < 0) dif = 0;
    return (int)((dif + 30) / 60);
  }
  return (int)(((millis() - readingMillis) / 1000 + 30) / 60);
}

void GlucoseState::valueString(char *out, size_t outLen, bool asMgdl) const {
  if (!hasData) { strlcpy(out, "---", outLen); return; }
  if (asMgdl) snprintf(out, outLen, "%u", mgdl);
  else        snprintf(out, outLen, "%.1f", mgdl / 18.0f);
}

void GlucoseState::deltaString(char *out, size_t outLen, bool asMgdl) const {
  if (!deltaValid) { strlcpy(out, "---", outLen); return; }
  if (asMgdl) snprintf(out, outLen, "%+d", deltaMgdl);
  else        snprintf(out, outLen, "%+.1f", deltaMgdl / 18.0f);
}

void GlucoseState::persist() {
  Preferences p;
  p.begin(NVS_NS, false);
  p.putBytes("hist", hist, sizeof(hist));
  p.putUChar("cnt", histCount);
  p.putUShort("mgdl", mgdl);
  p.putLong64("utc", (int64_t)readingUtc);
  p.end();
}

void GlucoseState::restore() {
  Preferences p;
  p.begin(NVS_NS, true);
  if (p.getBytesLength("hist") == sizeof(hist)) {
    p.getBytes("hist", hist, sizeof(hist));
    histCount = p.getUChar("cnt", 0);
    uint16_t v = p.getUShort("mgdl", 0);
    time_t utc = (time_t)p.getLong64("utc", 0);
    if (v >= 10 && v <= 600) {
      mgdl = v;
      readingUtc = utc;
      readingMillis = millis();
      hasData = true;
      arrowAngle = ARROW_HIDDEN;   // trend unknown after reboot
    }
  }
  p.end();
}

int nsDirectionToAngle(const char *dir) {
  if (!dir) return ARROW_HIDDEN;
  if (strcmp(dir, "DoubleDown") == 0)    return 90;
  if (strcmp(dir, "SingleDown") == 0)    return 75;
  if (strcmp(dir, "FortyFiveDown") == 0) return 45;
  if (strcmp(dir, "Flat") == 0)          return 0;
  if (strcmp(dir, "FortyFiveUp") == 0)   return -45;
  if (strcmp(dir, "SingleUp") == 0)      return -75;
  if (strcmp(dir, "DoubleUp") == 0)      return -90;
  return ARROW_HIDDEN;
}

int obbTrendToAngle(uint8_t trend) {
  switch (trend) {                        // OBB spec 3.2 trend enum
    case 1: return -90;                   // double up
    case 2: return -75;                   // single up
    case 3: return -45;                   // forty-five up
    case 4: return 0;                     // flat
    case 5: return 45;                    // forty-five down
    case 6: return 75;                    // single down
    case 7: return 90;                    // double down
    default: return ARROW_HIDDEN;         // unknown / out of range
  }
}
