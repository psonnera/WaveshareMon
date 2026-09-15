/*
  Alarms.h - glucose alarms, sounds and snooze
  (part of WaveshareMon, GPL v3, see LICENSE; derived from M5Stack_xDripMon)

  Timing is kept on the wall clock (time()), which the ESP32 preserves across
  deep sleep, so snooze and repeat intervals survive the power cycle.

  Copyright (C) 2026 Patrick Sonnerat
*/
#ifndef ALARMS_H
#define ALARMS_H

#include <Arduino.h>
#include <time.h>

enum AlarmState : uint8_t {
  ALARM_NONE = 0,
  ALARM_WARN_LOW,
  ALARM_WARN_HIGH,
  ALARM_WARN_NOREAD,
  ALARM_ALARM_LOW,
  ALARM_ALARM_HIGH,
  ALARM_REMOTE,          // raised by xDrip over OBB (text in remoteLabel)
};

class Alarms {
public:
  void tick();                 // evaluate + sound; call from loop (rate-limits itself)
  void evaluateNow();          // same, without the rate limit (end of a wake window)
  void snooze();               // BOOT button
  void clearSnooze();
  AlarmState state() const { return current; }
  const char *label() const;   // text for the bottom bar, "" when none
  int  snoozeRemainingMin() const;
  bool isSnoozed() const { return snoozeRemainingMin() > 0; }
  // OBB alarm characteristic: type per spec (0 = all clear)
  void onRemoteAlarm(uint8_t type, uint16_t mgdl);
  // blocking test sounds (setup command)
  void testSound(bool isAlarm) { sound(isAlarm); }
  // earliest wall-clock time the device must be awake for an alarm event
  // (repeat, snooze expiry, no-readings threshold); 0 = nothing pending
  time_t nextWakeUtc(time_t now) const;
  // deep-sleep persistence (RTC memory)
  void save();
  void restore();
  // set by tick when the bottom bar needs a redraw
  volatile bool stateChanged = false;

private:
  AlarmState evaluate() const;
  void sound(bool isAlarm);
  void run();
  AlarmState current = ALARM_NONE;
  uint32_t lastEvalMs = 0;
  time_t   lastSoundUtc = 0;
  bool everSounded = false;
  time_t   snoozeUntilUtc = 0;
  int  snoozeMult = 0;
  uint32_t lastSnoozePressMs = 0;
  uint8_t  remoteType = 0;
  time_t   remoteUtc = 0;
};

extern Alarms alarms;

#endif
