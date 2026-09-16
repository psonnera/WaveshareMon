/*
  BoardPower.h - power switches, battery latch and deep-sleep wake pins per board
  (part of WaveshareMon, GPL v3, see LICENSE)

  The S3 boards switch the panel supply, the audio rail and the battery latch
  with GPIOs that are held through deep sleep; the C6 board drives the same
  three switches through a TCA9554 I2C expander whose outputs simply keep
  their state while the chip sleeps. Callers never see the difference.

  Copyright (C) 2026 Patrick Sonnerat
*/
#ifndef BOARDPOWER_H
#define BOARDPOWER_H

#include <Arduino.h>

// first thing after a boot or a wake: keep the battery latch, switch the
// rails on, release the sleep holds, hand the wake pins back to GPIO. Starts
// the I2C bus (the expander lives on it).
void boardPowerBegin();
void boardPanelPower(bool on);
void boardAudioPower(bool on);
void boardBatteryLatch(bool on);
void boardLed(bool on);                  // C6 only (no LED on the S3 boards)
// before deep sleep: hold the switches, arm the button wake (PWR, plus BOOT
// where the chip can wake on it). pwrOnly = the power-off sleep
void boardPrepareSleep(bool pwrOnly);
// power off: the battery latch is released (on battery the board really
// powers down), PWR pulled low starts it again (wakes it on USB)
void boardPowerOff();
// which button woke the chip (after an EXT1 wake)
bool boardWakeWasPwr();

#endif
