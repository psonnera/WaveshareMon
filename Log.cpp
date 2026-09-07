#include "Log.h"
#include "AppConfig.h"
#include <stdarg.h>
#include <time.h>

static LogEntry entries[LOG_ENTRIES];
static int count = 0;
static int head = 0;            // next write slot
static uint32_t total = 0;      // entries ever logged
volatile bool logDirty = false;

void logAdd(const char *fmt, ...) {
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

  Serial.print("[log] ");
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

uint32_t logTotal() { return total; }

const LogEntry *logGet(int idx) {
  if (idx < 0 || idx >= count) return nullptr;
  int pos = (head - 1 - idx + 2 * LOG_ENTRIES) % LOG_ENTRIES;
  return &entries[pos];
}
