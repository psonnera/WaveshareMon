/*
  BleXdrip4iOS.h - "M5Stack" protocol of xDrip4iOS / xdripswift: the iPhone pushes readings to us
  (part of WaveshareMon, GPL v3, see LICENSE; ported from M5Stack_xDripMon)

  xDrip4iOS supports a Bluetooth peripheral of type "M5Stack": one GATT
  characteristic (write + notify) carrying 20-byte frames
  [opcode][packet no][packets][ascii]. No bonding: the app authenticates with
  a 10-character password that the device generates on first contact and hands
  to the app itself, so nothing has to be typed on the phone. The app then
  sends every reading ("mgdl epoch"), the trend, the local time and time zone,
  the units and, when set in the app, the Nightscout and Wi-Fi settings.

  The app finds a new device by its name containing "M5Stack" (case-insensitive)
  and later by its address, so in this mode the device calls itself
  "M5Stack WaveshareMon-XXXX" on its stable static address (see bleBegin()).
  The service lives next to the setup service on the same NimBLE server; the
  setup server owns advertising and the connection callbacks and calls the
  hooks below.

  Copyright (C) 2026 Patrick Sonnerat
*/
#ifndef BLEXDRIP4IOS_H
#define BLEXDRIP4IOS_H

#include <Arduino.h>

// "M5Stack <device name>", the advertised name in this mode
const char *xdrip4iosName();
// create the service (after setupServerBegin())
void xdrip4iosBegin();
void xdrip4iosTick();
// drop the phone before deep sleep
void xdrip4iosStop();
bool xdrip4iosConnected();
bool xdrip4iosIsAuthenticated();
// off / not paired / waiting / auth / connected
const char *xdrip4iosStateName();
// forget the password: the next app that asks gets a fresh one (remove the
// device in xDrip4iOS and add it again)
void xdrip4iosForgetPassword();
// connection hooks called by the setup server's callbacks
void xdrip4iosOnConnect(uint16_t connHandle);
void xdrip4iosOnDisconnect(uint16_t connHandle);

#endif
