/*
  BleMiBand.h - Mi Band 2 emulation: xDrip+ pushes readings to us directly
  (part of WaveshareMon, GPL v3, see LICENSE; ported from M5Stack_xDripMon)

  xDrip+'s built-in Mi Band support (Settings > Smart Watch Features > Mi Band)
  connects to a device named "MI Band 2", authenticates with a 16-byte AES key
  and writes every new reading as a text alert ("BG: 123 ->"). No phone-side
  companion app is needed. Only the board's GATT server is involved: the
  services live next to the setup service on the same NimBLE server.

  Copyright (C) 2026 Patrick Sonnerat
*/
#ifndef BLEMIBAND_H
#define BLEMIBAND_H

#include <Arduino.h>

// create the services (after setupServerBegin()) and start advertising
void miBandBegin();
void miBandTick();
// drop the phone before deep sleep
void miBandStop();
bool miBandConnected();
bool miBandIsAuthenticated();
const char *miBandStateName();
// DeviceEvent CALL_REJECT -> snoozes the alert on the phone
void miBandSendSnooze();
// forget the AES key (the phone must pair again: clear its MAC in xDrip)
void miBandForgetKey();

#endif
