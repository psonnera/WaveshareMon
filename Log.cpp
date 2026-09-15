#include <esp_attr.h>
#include "Log.h"
#include "AppConfig.h"
#include <stdarg.h>
#include <time.h>

// The ring lives in RTC slow memory so the log survives the deep-sleep power
// cycle (a cold boot clears it).
RTC_DATA_ATTR static LogEntry entries[LOG_ENTRIES];
RTC_DATA_ATTR static int count = 0;
RTC_DATA_ATTR static int head = 0;            // next write slot
RTC_DATA_ATTR static uint32_t total = 0;      // entries ever logged
volatile bool logDirty = false;

void logAdd(const char *fmt, ...) {
  if (count < 0 || count > LOG_ENTRIES || head < 0 || head >= LOG_ENTRIES) {
    count = 0; head = 0; total = 0;           // RTC memory garbage after a brown-out
  }
  LogEntry &e = entries[head];
  va_list args;
  va_start(args, fmt);
  vsnprintf(e.text, sizeof(e.text), fmt, args);
  va_end(args);
  e.ms = millis();
  time_t now = time(nullptr);
  e.utc = now > 1600000000 ? now : 0;

  head = (head + 1) % LOG_ENTRIES;
  if (count < LOG_ENTRIES) count++;
  total++;
  logDirty = true;

  char stamp[16];
  logStamp(&e, stamp, sizeof(stamp));
  Serial.printf("[%s] ", stamp);
  Serial.println(e.text);
}

void logDebug(const char *fmt, ...) {
  if (!cfg.debugLog) return;
  char buf[LOG_LINE_LEN];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  logAdd("~%s", buf);
}

void logStamp(const LogEntry *e, char *out, size_t len) {
  if (e->utc) {
    struct tm lt;
    localtime_r(&e->utc, &lt);
    snprintf(out, len, "%02d:%02d:%02d", lt.tm_hour, lt.tm_min, lt.tm_sec);
  } else {
    snprintf(out, len, "%6lus", (unsigned long)(e->ms / 1000));
  }
}

uint32_t logTotal() { return total; }

const LogEntry *logGet(int idx) {
  if (idx < 0 || idx >= count) return nullptr;
  int pos = (head - 1 - idx + 2 * LOG_ENTRIES) % LOG_ENTRIES;
  return &entries[pos];
}
