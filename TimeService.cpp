/*
  TimeService.cpp - wall clock handling
  (part of WaveshareMon, GPL v3, see LICENSE; derived from M5Stack_xDripMon)

  Copyright (C) 2026 Patrick Sonnerat
*/
#include <esp_attr.h>
#include "TimeService.h"
#include "AppConfig.h"
#include "Board.h"
#include "Log.h"
#include <Wire.h>
#include <sys/time.h>
#include <esp_sntp.h>

TimeService timeService;

// interpret tm fields as UTC and return the epoch, independent of the TZ env
static time_t epochFromUtcTm(struct tm &t) {
  char *oldTz = getenv("TZ") ? strdup(getenv("TZ")) : nullptr;
  setenv("TZ", "UTC0", 1); tzset();
  time_t epoch = mktime(&t);
  if (oldTz) { setenv("TZ", oldTz, 1); free(oldTz); } else unsetenv("TZ");
  tzset();
  return epoch;
}

time_t TimeService::utcFromTm(struct tm &t) { return epochFromUtcTm(t); }

// fixed offset as a POSIX string. POSIX sign is inverted (UTC+2 -> "LOC-2")
// and newlib needs a >= 3 letter zone name or tzset() silently stays on UTC.
static void offsetTz(int32_t tzSec, char *tz, size_t len) {
  int32_t off = -tzSec;
  char sign = (off < 0) ? '-' : '+';
  uint32_t a = (off < 0) ? (uint32_t)-off : (uint32_t)off;
  unsigned h = a / 3600, m = (a % 3600) / 60;
  if (m) snprintf(tz, len, "LOC%c%u:%02u", sign, h, m);
  else   snprintf(tz, len, "LOC%c%u", sign, h);
}

void TimeService::applyTz() {
  char tz[48];
  if (cfg.tzString[0]) strlcpy(tz, cfg.tzString, sizeof(tz));
  else offsetTz(cfg.tzOffsetSec, tz, sizeof(tz));
  setenv("TZ", tz, 1);
  tzset();
}

// ---- PCF85063 (UTC kept in the RTC) ------------------------------------------

static uint8_t bcd2bin(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t bin2bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

bool TimeService::readRtc(time_t &utc) {
  Wire.beginTransmission(I2C_ADDR_RTC);
  Wire.write(0x04);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)I2C_ADDR_RTC, (uint8_t)7) != 7) return false;
  uint8_t r[7];
  for (int i = 0; i < 7; i++) r[i] = Wire.read();
  if (r[0] & 0x80) return false;                 // OS flag: oscillator stopped, time invalid
  struct tm t = {};
  t.tm_sec  = bcd2bin(r[0] & 0x7F);
  t.tm_min  = bcd2bin(r[1] & 0x7F);
  t.tm_hour = bcd2bin(r[2] & 0x3F);
  t.tm_mday = bcd2bin(r[3] & 0x3F);
  t.tm_mon  = bcd2bin(r[5] & 0x1F) - 1;
  t.tm_year = bcd2bin(r[6]) + 100;               // 2000-based
  if (t.tm_year + 1900 < 2024) return false;
  utc = epochFromUtcTm(t);
  return true;
}

void TimeService::writeRtc(time_t utc) {
  struct tm t;
  gmtime_r(&utc, &t);
  Wire.beginTransmission(I2C_ADDR_RTC);
  Wire.write(0x04);
  Wire.write(bin2bcd(t.tm_sec));                 // clears the OS flag
  Wire.write(bin2bcd(t.tm_min));
  Wire.write(bin2bcd(t.tm_hour));
  Wire.write(bin2bcd(t.tm_mday));
  Wire.write(t.tm_wday);
  Wire.write(bin2bcd(t.tm_mon + 1));
  Wire.write(bin2bcd(t.tm_year - 100));
  Wire.endTransmission();
}

// ---- public ------------------------------------------------------------------

void TimeService::begin() {
  applyTz();
  // make sure the RTC runs (CONTROL_1: 12.5 pF, normal mode). Right after a
  // reset the chip is sometimes not ready yet: retry briefly.
  bool rtcOk = false;
  for (int attempt = 0; attempt < 4 && !rtcOk; attempt++) {
    if (attempt) delay(50);
    Wire.beginTransmission(I2C_ADDR_RTC);
    Wire.write(0x00); Wire.write(0x00);
    rtcOk = Wire.endTransmission() == 0;
  }
  time_t utc;
  if (rtcOk && readRtc(utc)) {
    struct timeval tv = { .tv_sec = utc, .tv_usec = 0 };
    settimeofday(&tv, nullptr);
    timeKnown = true;
    Serial.println("[time] restored from RTC");
  } else if (time(nullptr) > 1600000000) {
    // the ESP32 keeps the system clock across deep sleep and soft resets
    timeKnown = true;
    Serial.println(rtcOk ? "[time] RTC not set, kept by ESP32" : "[time] RTC not found, kept by ESP32");
    if (rtcOk) writeRtc(time(nullptr));
  } else {
    Serial.println(rtcOk ? "[time] RTC not set" : "[time] RTC not found");
  }
}

void TimeService::setFromUtc(time_t utc, int32_t tzSec) {
  // the phone's offset is authoritative while xDrip is the source
  bool changed = cfg.tzOffsetSec != tzSec || cfg.tzString[0] != 0;
  cfg.tzOffsetSec = tzSec;
  cfg.tzString[0] = 0;
  applyTz();
  bool wasKnown = timeKnown;
  setFromUtc(utc);
  if (changed) cfg.save();
  if (changed || !wasKnown) logAdd("time set by phone (UTC%+ld)", (long)tzSec / 3600);
}

void TimeService::setFromUtc(time_t utc) {
  struct timeval tv = { .tv_sec = utc, .tv_usec = 0 };
  settimeofday(&tv, nullptr);
  writeRtc(utc);
  timeKnown = true;
}

void TimeService::setManual(int year, int month, int day, int hour, int minute, int second) {
  struct tm t = {};
  t.tm_year = year - 1900; t.tm_mon = month - 1; t.tm_mday = day;
  t.tm_hour = hour; t.tm_min = minute; t.tm_sec = second;
  time_t asIfUtc = epochFromUtcTm(t);
  setFromUtc(asIfUtc - cfg.tzOffsetSec);
}

// last successful NTP sync, kept across deep sleep: with the RTC keeping time
// a sync twice a day is plenty and the wake windows stay short
RTC_DATA_ATTR static int64_t s_lastNtpUtc = 0;

void TimeService::startNtp() {
  if (ntpStarted) return;
  time_t now = time(nullptr);
  if (timeKnown && s_lastNtpUtc && difftime(now, (time_t)s_lastNtpUtc) < 12 * 3600) return;
  ntpStarted = true;
  ntpSynced = false;
  // TZ is handled by applyTz(); configTime would overwrite it, so use UTC here
  configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");
  applyTz();
}

void TimeService::tick() {
  if (!ntpStarted || ntpSynced) return;
  time_t now = time(nullptr);
  if (now > 1600000000 && (!timeKnown || sntpSynced())) {
    ntpSynced = true;
    timeKnown = true;
    writeRtc(now);
    s_lastNtpUtc = (int64_t)now;
    logAdd("time set by NTP");
  }
}

bool TimeService::sntpSynced() const {
  return sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED;
}

bool TimeService::getLocalTm(struct tm &out) {
  if (!timeKnown) return false;
  time_t now = time(nullptr);
  localtime_r(&now, &out);
  return true;
}

void TimeService::formatTime(time_t t, char *out, size_t len) {
  if (!timeKnown || t == 0) { if (len) out[0] = 0; return; }
  struct tm lt;
  localtime_r(&t, &lt);
  if (cfg.timeFormat24) snprintf(out, len, "%02d:%02d", lt.tm_hour, lt.tm_min);
  else {
    int h = lt.tm_hour % 12; if (h == 0) h = 12;
    snprintf(out, len, "%d:%02d%s", h, lt.tm_min, lt.tm_hour < 12 ? "am" : "pm");
  }
}
