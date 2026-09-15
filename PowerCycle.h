/*
  PowerCycle.h - wake classification, radio window and deep-sleep scheduling
  (part of WaveshareMon, GPL v3, see LICENSE)

  A configured device does not run continuously: it wakes every reading period
  (5 min), opens a short radio window to fetch the latest value, redraws the
  e-paper when something changed and deep-sleeps again. The next wake is
  re-anchored on the timestamp of the latest reading. Cold boots, the first
  (unconfigured) run, setup mode and the "nosleep" debug flag keep the firmware
  awake in the classic always-on loop.

  Copyright (C) 2026 Patrick Sonnerat
*/
#ifndef POWERCYCLE_H
#define POWERCYCLE_H

#include <Arduino.h>

enum WakeKind : uint8_t { WAKE_COLD = 0, WAKE_TIMER, WAKE_BUTTON_BOOT, WAKE_BUTTON_PWR };

// setup advertising / awake period after a cold boot or a BOOT long press
#define SETUP_WINDOW_MS   (10UL * 60 * 1000)

// first thing in setup(): classifies the wake, restores the power rails
void cycleBegin();
WakeKind cycleWakeKind();
const char *cycleWakeName();
uint32_t cycleWakes();               // wakes since the last cold boot (1 = cold boot)

// keep the firmware awake at least this long (setup mode); extends only
void cycleStayAwake(uint32_t ms);
// true while the always-on loop is wanted (setup, first run, nosleep, awake hold)
bool cycleAwake();
// serial command: end the radio window now
void cycleSleepNow();
// call from loop() after the data modules ticked; sleeps when the window is over
void cycleTick();
// bottom-bar status for the sleeping modes ("" = all fine)
const char *cycleStatusText();
// "source: state" for the panel's status page and the Info JSON, e.g.
// "xDrip Mi Band: waiting for xDrip", "Wi-Fi: not configured", "Dexcom: bad login"
void cycleSourceStatus(char *out, size_t len);

#endif
