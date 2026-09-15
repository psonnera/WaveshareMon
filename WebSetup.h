/*
  WebSetup.h - configuration page over Wi-Fi during the setup window
  (part of WaveshareMon, GPL v3, see LICENSE)

  For people without the Android app (or without an Android phone at all).
  While the device is in setup mode (10 minutes after a cold boot or a BOOT
  long press, permanently while unconfigured) it opens an access point named
  after itself (open network, 192.168.4.1) with a captive portal, and serves
  the same page on its station address when a Wi-Fi source is connected. The
  page reads and writes the very JSON the BLE setup service uses, so both
  ways of configuring stay identical. Plain HTTP, no login: it exists only
  for the setup window, then the access point goes away.

  Copyright (C) 2026 Patrick Sonnerat
*/
#ifndef WEBSETUP_H
#define WEBSETUP_H

#include <Arduino.h>

// from loop(): starts with the setup window, stops with it, serves requests
void webSetupTick();
bool webSetupActive();
// "192.168.4.1" (the access point) - what the status page shows
const char *webSetupApIp();

#endif
