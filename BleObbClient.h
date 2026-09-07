/*
  BleObbClient.h - xDrip Open Bluetooth Broadcast receiver (BLE central)
  (part of WaveshareMon, GPL v3, see LICENSE)

  Implements the receiver side of the OBB spec v0.1: scan for the OBB
  service, connect, bond (Just Works), read Status, subscribe to Glucose,
  Alarm and (optionally) Status Line, parse the fixed binary packets.

  Copyright (C) 2026 Patrick Sonnerat
*/
#ifndef BLEOBBCLIENT_H
#define BLEOBBCLIENT_H

#include <Arduino.h>

enum ObbState : uint8_t { OBB_IDLE = 0, OBB_SCANNING, OBB_CONNECTING, OBB_CONNECTED };

// call once after NimBLEDevice::init()
void obbBegin();
// drive scan / connect / reconnect from the main loop
void obbTick();
void obbStop();                 // e.g. when the source changes
ObbState obbState();
const char *obbStateName();
// true while a setup client is connected (the same phone cannot hold two links)
void obbSetPaused(bool paused);
// set when connection state changes; consumed by the UI
extern volatile bool obbStateChanged;

#endif
