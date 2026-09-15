#ifndef LOG_H
#define LOG_H

#include <Arduino.h>

#define LOG_ENTRIES 30
#define LOG_LINE_LEN 44

// add a line to the on-device log page (also mirrored to Serial)
void logAdd(const char *fmt, ...);
// like logAdd, but only when cfg.debugLog is enabled (menu: Debug log);
// entries are prefixed with '~' to stand out from normal ones
void logDebug(const char *fmt, ...);

struct LogEntry {
  uint32_t ms;                 // millis() when logged
  time_t   utc;                // wall clock if known, else 0
  char     text[LOG_LINE_LEN];
};

// newest-first access for the log page; idx 0 = latest. Returns nullptr past end.
const LogEntry *logGet(int idx);
// "HH:MM:SS" local time when the clock was known, else seconds since boot
void logStamp(const LogEntry *e, char *out, size_t len);
// total number of entries ever logged (monotonic; for incremental readers)
uint32_t logTotal();
// set when a new entry arrives; consumed by the UI
extern volatile bool logDirty;

#endif
