/*
  GlucoseState.h - glucose readings, history and staleness tracking
  (part of WaveshareMon, GPL v3, see LICENSE; derived from M5Stack_xDripMon)

  Copyright (C) 2026 Patrick Sonnerat
*/
#ifndef GLUCOSESTATE_H
#define GLUCOSESTATE_H

#include <Arduino.h>
#include <time.h>

#define HIST_SIZE 48        // 4 hours at 5-minute cadence
#define ARROW_HIDDEN 180    // NightscoutMon convention

struct GlucoseState {
  uint16_t mgdl = 0;             // 0 = no data yet
  int      arrowAngle = ARROW_HIDDEN;
  time_t   readingUtc = 0;       // 0 = unknown wall-clock timestamp
  uint32_t readingMillis = 0;    // millis() when reading arrived (staleness fallback)
  bool     hasData = false;
  // false after boot (value restored from NVS) until a reading arrives from
  // the source; the UI crosses the value out while this is false
  bool     live = false;
  bool     remoteStale = false;  // OBB flag bit 0 (xDrip says it is old)

  // history for the mini graph (mg/dL, oldest first, newest last)
  uint16_t hist[HIST_SIZE] = {0};
  uint8_t  histCount = 0;

  int16_t  deltaMgdl = 0;        // vs previous distinct reading
  bool     deltaValid = false;

  // optional text from the source (Nightscout IOB/COB, OBB status line)
  char     infoLine[96] = "";

  // set by the data layer, consumed (cleared) by the UI
  volatile bool dataChanged = false;

  // counters for the power cycle: sourceSeq advances on every reading the
  // source delivered (duplicates included: the source was reached and has
  // nothing newer), readingSeq only on a genuinely new reading
  uint32_t sourceSeq = 0;
  uint32_t readingSeq = 0;

  // the source was changed / reconfigured: the panel shows its status page
  // again until the new source delivers a reading
  void markSourceChanged() { live = false; deltaValid = false; infoLine[0] = 0; dataChanged = true; }

  void onReading(uint16_t newMgdl, time_t utc, int arrowAngleIn);
  // authoritative delta from the source (OBB packet / Nightscout)
  void setDelta(int16_t delta, bool valid);
  // replace the history with values from the source (oldest first)
  void replaceHistory(const uint16_t *vals, uint8_t n);
  void setInfoLine(const char *s);

  float sgvMmol() const { return mgdl / 18.0f; }
  int   minutesAgo() const;
  // reading age on the screen: crossed out from LATE_MIN, replaced by "---" from STALE_MIN
  static const int LATE_MIN  = 7;
  static const int STALE_MIN = 12;
  bool  isLate()  const { return remoteStale || minutesAgo() >= LATE_MIN; }
  bool  isStale() const { return minutesAgo() >= STALE_MIN; }
  // value formatted in the configured units ("123" or "6.8")
  void  valueString(char *out, size_t outLen, bool asMgdl) const;
  void  deltaString(char *out, size_t outLen, bool asMgdl) const;

  void persist();                // last value + history to NVS (survives power loss)
  void restore();                // RTC copy after deep sleep, else NVS
  void saveRtc();                // full state to RTC memory before deep sleep
};

// map a Nightscout direction name ("Flat", "SingleUp", ...) to an arrow angle
int nsDirectionToAngle(const char *dir);
// map the OBB trend enum (0..8) to an arrow angle
int obbTrendToAngle(uint8_t trend);
// map an xDrip UTF-8 arrow character ("→", "↗", ...) to an arrow angle
int slopeArrowToAngle(const char *s);

extern GlucoseState gs;

#endif
