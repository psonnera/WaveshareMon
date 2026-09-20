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
uint32_t setupPasskey();                 // the pairing code of this boot (OBB mode: passkey pairing)
void     setupSetPasskey(uint32_t code); // drawn per boot by bleBegin
bool setupServerClientConnected();
// forward new log lines to a subscribed app; serve deferred commands
void setupServerTick();
// disconnect every BLE client (before deep sleep)
void setupServerDropClients();

// The same JSON and commands, shared with the Wi-Fi setup page (WebSetup.cpp)
// so both ways of configuring the device stay identical.
#include <string>
void setupBuildInfo(std::string &out);
void setupBuildConfig(std::string &out);
// applies and saves a Config JSON (any subset of keys); who = "app" / "web"
bool setupApplyConfig(const char *json, size_t len, const char *who);
// queues a Command-characteristic command ("reboot", "update", ...); false = unknown
bool setupCommand(const char *cmd);

#endif
