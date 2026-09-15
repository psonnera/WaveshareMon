/*
  Alarms.cpp - glucose alarms, sounds and snooze
  (part of WaveshareMon, GPL v3, see LICENSE)

  The warning/alarm tone patterns and the increasing-snooze behaviour are
  ported from M5_NightscoutMon (sndWarning/sndAlarm), Copyright (C)
  Martin Lukasek <martin@lukasek.cz>, GPL v3, via M5Stack_xDripMon.

  Copyright (C) 2026 Patrick Sonnerat
*/
#include <esp_attr.h>
#include "Alarms.h"
#include "AppConfig.h"
#include "Board.h"
#include "GlucoseState.h"
#include "Audio.h"
#include "Log.h"

Alarms alarms;

static inline time_t nowUtc() { return time(nullptr); }

// true while the BOOT button is held: the user wants the sound to stop
static bool snoozePressed() { return digitalRead(PIN_BOOT_BTN) == LOW; }

static bool play_tone(uint16_t frequency, uint32_t duration, uint8_t volume) {
  audio.tone(frequency, duration, volume);
  uint32_t start = millis();
  while (audio.isPlaying() && millis() - start < duration + 200) {
    if (snoozePressed()) return false;
    delay(1);
  }
  if (!audio.available()) delay(duration);
  return true;
}

static bool pause(uint32_t ms) {
  uint32_t start = millis();
  while (millis() - start < ms) {
    if (snoozePressed()) return false;
    delay(1);
  }
  return true;
}

void Alarms::sound(bool isAlarm) {
  bool done = true;
  if (isAlarm) {
    for (int j = 0; j < 6 && done; j++) {           // NightscoutMon sndAlarm()
      done = play_tone(660, 400, cfg.alarmVolume) && pause(200);
    }
  } else {
    for (int j = 0; j < 3 && done; j++) {           // NightscoutMon sndWarning()
      done = play_tone(3000, 100, cfg.warnVolume) && pause(300);
    }
  }
  if (!done) {
    audio.mute();
    snooze();
    while (snoozePressed()) delay(5);             // wait for the release
  }
}

AlarmState Alarms::evaluate() const {
  if (!cfg.alarmsEnabled) return ALARM_NONE;
  // a remote alarm stays displayed for 30 min unless cleared by xDrip
  if (remoteType != 0 && difftime(nowUtc(), remoteUtc) < 30 * 60) return ALARM_REMOTE;
  if (!gs.hasData) return ALARM_NONE;
  if (!gs.isStale()) {
    // a value the screen no longer shows ("---") cannot raise a low / high;
    // only the no-readings warning applies then
    uint16_t v = gs.mgdl;
    if (v >= 10 && v <= cfg.alarmLow)  return ALARM_ALARM_LOW;
    if (v >= 10 && v <= cfg.warnLow)   return ALARM_WARN_LOW;
    if (v >= cfg.alarmHigh)            return ALARM_ALARM_HIGH;
    if (v >= cfg.warnHigh)             return ALARM_WARN_HIGH;
  }
  if (gs.minutesAgo() >= (int)cfg.noReadingsMin) return ALARM_WARN_NOREAD;
  return ALARM_NONE;
}

static const char *remoteLabel(uint8_t type) {
  switch (type) {
    case 1: return "URGENT LOW";
    case 2: return "LOW";
    case 3: return "HIGH";
    case 4: return "URGENT HIGH";
    case 5: return "MISSED READINGS";
    case 6: return "SENSOR PROBLEM";
    case 7: return "PHONE BATTERY";
    default: return "ALERT";
  }
}

const char *Alarms::label() const {
  switch (current) {
    case ALARM_WARN_LOW:    return "WARNING LOW";
    case ALARM_WARN_HIGH:   return "WARNING HIGH";
    case ALARM_WARN_NOREAD: return "NO READINGS";
    case ALARM_ALARM_LOW:   return "ALARM LOW";
    case ALARM_ALARM_HIGH:  return "ALARM HIGH";
    case ALARM_REMOTE:      return remoteLabel(remoteType);
    default:                return "";
  }
}

void Alarms::tick() {
  uint32_t now = millis();
  if (now - lastEvalMs < 1000) return;
  lastEvalMs = now;
  run();
}

void Alarms::evaluateNow() {
  lastEvalMs = millis();
  run();
}

void Alarms::run() {
  AlarmState s = evaluate();
  if (s != current) {
    static const char *names[] = {"ok", "warn low", "warn high", "no data",
                                  "ALARM LOW", "ALARM HIGH"};
    logAdd("alarm: %s", s == ALARM_REMOTE ? remoteLabel(remoteType) : names[s]);
    current = s;
    stateChanged = true;
    if (s == ALARM_NONE) everSounded = false;   // next episode sounds immediately
  }
  if (s == ALARM_NONE) return;
  if (isSnoozed()) return;

  time_t t = nowUtc();
  bool repeatDue = !everSounded ||
                   difftime(t, lastSoundUtc) > (double)cfg.alarmRepeatMin * 60;
  if (!repeatDue) return;

  lastSoundUtc = t;
  everSounded = true;
  bool urgent = s == ALARM_ALARM_LOW || s == ALARM_ALARM_HIGH ||
                (s == ALARM_REMOTE && (remoteType == 1 || remoteType == 4));
  sound(urgent);
}

void Alarms::onRemoteAlarm(uint8_t type, uint16_t mgdl) {
  if (type == 0) {
    if (remoteType) logAdd("xDrip: all clear");
    remoteType = 0;
    return;
  }
  logAdd("xDrip alarm %s (%u)", remoteLabel(type), mgdl);
  remoteType = type;
  remoteUtc = nowUtc();
  everSounded = false;          // sound right away even if a local alarm was already on
  lastEvalMs = 0;
}

void Alarms::snooze() {
  if (current == ALARM_NONE && !isSnoozed()) return;
  uint32_t now = millis();
  if (lastSnoozePressMs && now - lastSnoozePressMs < 2000) {   // rapid re-press: extend
    snoozeMult++;
    if (snoozeMult > 4) snoozeMult = 0;   // ...then cycle back to OFF
  } else {
    snoozeMult = 1;
  }
  lastSnoozePressMs = now;
  if (snoozeMult == 0)
    snoozeUntilUtc = 0;
  else
    snoozeUntilUtc = nowUtc() + (time_t)snoozeMult * cfg.snoozeMin * 60;
  audio.mute();
  logAdd("snooze %d min", snoozeMult * cfg.snoozeMin);
  stateChanged = true;
}

void Alarms::clearSnooze() {
  snoozeUntilUtc = 0;
  snoozeMult = 0;
  stateChanged = true;
}

int Alarms::snoozeRemainingMin() const {
  if (snoozeUntilUtc == 0) return 0;
  double left = difftime(snoozeUntilUtc, nowUtc());
  if (left <= 0) return 0;
  return (int)((left + 59) / 60);
}

time_t Alarms::nextWakeUtc(time_t now) const {
  if (!cfg.alarmsEnabled) return 0;
  time_t next = 0;
  auto consider = [&next, now](time_t t) {
    if (t <= now) t = now;
    if (!next || t < next) next = t;
  };
  if (current != ALARM_NONE) {
    if (isSnoozed()) consider(snoozeUntilUtc);                       // re-sound when it expires
    else consider(lastSoundUtc + (time_t)cfg.alarmRepeatMin * 60);   // next repeat
  } else if (gs.hasData && gs.readingUtc) {
    consider(gs.readingUtc + (time_t)cfg.noReadingsMin * 60);        // "no readings" threshold
  }
  return next;
}

// ---- deep-sleep persistence -----------------------------------------------------

struct AlarmRtc {
  uint32_t magic;
  uint8_t  current, everSounded, remoteType, snoozeMult;
  int64_t  lastSoundUtc, snoozeUntilUtc, remoteUtc;
};
#define ALARM_MAGIC 0x414C4D31UL
RTC_DATA_ATTR static AlarmRtc s_rtc;

void Alarms::save() {
  s_rtc.magic = ALARM_MAGIC;
  s_rtc.current = current;
  s_rtc.everSounded = everSounded;
  s_rtc.remoteType = remoteType;
  s_rtc.snoozeMult = snoozeMult;
  s_rtc.lastSoundUtc = lastSoundUtc;
  s_rtc.snoozeUntilUtc = snoozeUntilUtc;
  s_rtc.remoteUtc = remoteUtc;
}

void Alarms::restore() {
  if (s_rtc.magic != ALARM_MAGIC) return;
  current = (AlarmState)s_rtc.current;
  everSounded = s_rtc.everSounded;
  remoteType = s_rtc.remoteType;
  snoozeMult = s_rtc.snoozeMult;
  lastSoundUtc = (time_t)s_rtc.lastSoundUtc;
  snoozeUntilUtc = (time_t)s_rtc.snoozeUntilUtc;
  remoteUtc = (time_t)s_rtc.remoteUtc;
}
