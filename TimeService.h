/*
  TimeService.h - wall clock: PCF85063 RTC, NTP, OBB status, POSIX TZ
  (part of WaveshareMon, GPL v3, see LICENSE; derived from M5Stack_xDripMon)

  Copyright (C) 2026 Patrick Sonnerat
*/
#ifndef TIMESERVICE_H
#define TIMESERVICE_H

#include <Arduino.h>
#include <time.h>

class TimeService {
public:
  void begin();                                   // apply TZ, restore from RTC
  void applyTz();                                 // after a config change
  void setFromUtc(time_t utc, int32_t tzSec);     // OBB status (fixed offset)
  void setFromUtc(time_t utc);                    // NTP: keep the configured TZ
  void setManual(int year, int month, int day, int hour, int minute, int second = 0);
  void startNtp();                                // when Wi-Fi is up
  void tick();                                    // picks up NTP sync
  bool known() const { return timeKnown; }
  bool getLocalTm(struct tm &out);                // false if time unknown
  // "HH:MM" per cfg.timeFormat24, "" if unknown
  void formatTime(time_t t, char *out, size_t len);

private:
  void writeRtc(time_t utc);
  bool readRtc(time_t &utc);
  bool timeKnown = false;
  bool ntpStarted = false;
  bool ntpSynced = false;
};

extern TimeService timeService;

#endif
