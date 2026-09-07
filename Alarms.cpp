/*
  Alarms.cpp - glucose alarms, sounds and snooze
  (part of WaveshareMon, GPL v3, see LICENSE)

  The warning/alarm tone patterns and the increasing-snooze behaviour are
  ported from M5_NightscoutMon (sndWarning/sndAlarm), Copyright (C)
  Martin Lukasek <martin@lukasek.cz>, GPL v3, via M5Stack_xDripMon.

  Copyright (C) 2026 Patrick Sonnerat
*/
#include "Alarms.h"
#include "AppConfig.h"
#include "GlucoseState.h"
#include "Audio.h"
#include "Log.h"

Alarms alarms;

static void play_tone(uint16_t frequency, uint32_t duration, uint8_t volume) {
  audio.tone(frequency, duration, volume);
  uint32_t start = millis();
  while (audio.isPlaying() && millis() - start < duration + 200)
    delay(1);
  if (!audio.available()) delay(duration);
}

void Alarms::sound(bool isAlarm) {
  if (isAlarm) {
    for (int j = 0; j < 6; j++) {           // NightscoutMon sndAlarm()
      play_tone(660, 400, cfg.alarmVolume);
      delay(200);
    }
  } else {
    for (int j = 0; j < 3; j++) {           // NightscoutMon sndWarning()
      play_tone(3000, 100, cfg.warnVolume);
      delay(300);
    }
  }
}

AlarmState Alarms::evaluate() const {
  if (!cfg.alarmsEnabled) return ALARM_NONE;
  // a remote alarm stays displayed for 30 min unless cleared by xDrip
  if (remoteType != 0 && millis() - remoteMs < 30UL * 60000UL) return ALARM_REMOTE;
  if (!gs.hasData) return ALARM_NONE;
  uint16_t v = gs.mgdl;
  if (v >= 10 && v <= cfg.alarmLow)  return ALARM_ALARM_LOW;
  if (v >= 10 && v <= cfg.warnLow)   return ALARM_WARN_LOW;
  if (v >= cfg.alarmHigh)            return ALARM_ALARM_HIGH;
  if (v >= cfg.warnHigh)             return ALARM_WARN_HIGH;
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

  bool repeatDue = !everSounded ||
                   (now - lastSoundMs) > (uint32_t)cfg.alarmRepeatMin * 60000UL;
  if (!repeatDue) return;

  lastSoundMs = now;
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
  remoteMs = millis();
  everSounded = false;          // sound right away even if a local alarm was already on
  lastEvalMs = 0;
}

void Alarms::snooze() {
  if (current == ALARM_NONE && !isSnoozed()) return;
  uint32_t now = millis();
  if (now - lastSnoozePressMs < 2000) {   // rapid re-press: extend the snooze
    snoozeMult++;
    if (snoozeMult > 4) snoozeMult = 0;   // ...then cycle back to OFF
  } else {
    snoozeMult = 1;
  }
  lastSnoozePressMs = now;
  if (snoozeMult == 0)
    snoozeUntilMs = 0;
  else
    snoozeUntilMs = now + (uint32_t)snoozeMult * cfg.snoozeMin * 60000UL;
  audio.mute();
  logAdd("snooze %d min", snoozeMult * cfg.snoozeMin);
  stateChanged = true;
}

void Alarms::clearSnooze() {
  snoozeUntilMs = 0;
  snoozeMult = 0;
  stateChanged = true;
}

int Alarms::snoozeRemainingMin() const {
  uint32_t now = millis();
  if (snoozeUntilMs == 0 || (int32_t)(snoozeUntilMs - now) <= 0) return 0;
  return (int)((snoozeUntilMs - now + 59999) / 60000);
}
