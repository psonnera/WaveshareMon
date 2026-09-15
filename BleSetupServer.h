/*
  BleSetupServer.h - device configuration over BLE for the xDrip OBB app
  (part of WaveshareMon, GPL v3, see LICENSE)

  The board has no buttons or keyboard, so Wi-Fi, Nightscout and display
  settings are written by the Android app through this small GATT service:
    4d5f0001-2b8c-4a3e-9f61-7c2d9e8b5a10  service
    4d5f0002-...  Info    READ           JSON status (plain)
    4d5f0003-...  Config  READ | WRITE   JSON settings (encrypted, bonded)
    4d5f0004-...  Command WRITE          text command (encrypted, bonded)
    4d5f0005-...  Log     NOTIFY         log lines (plain)
    4d5f0006-...  Scan    READ           JSON Wi-Fi scan result (plain); the
                                         "wifiscan" command starts a scan

  Copyright (C) 2026 Patrick Sonnerat
*/
#ifndef BLESETUPSERVER_H
#define BLESETUPSERVER_H

#include <Arduino.h>

// create the service (once, after NimBLEDevice::init())
void setupServerBegin();
// start / stop advertising the setup service
void setupServerAdvertise(bool on);
bool setupServerAdvertising();
bool setupServerClientConnected();
// forward new log lines to a subscribed app; serve deferred commands
void setupServerTick();
// disconnect every BLE client (before deep sleep)
void setupServerDropClients();

#endif
