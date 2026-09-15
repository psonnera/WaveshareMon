/*
  LibreLinkUpClient.h - LibreLinkUp (LibreView follower) API polling
  (part of WaveshareMon, GPL v3, see LICENSE)

  Speaks the unofficial "LLU v4" API used by nightscout-librelink-up,
  xdripswift and GlucoseDirect: login, connections, graph. The account must
  be a LibreLinkUp follower invited from the patient's LibreLink app. Abbott
  changes the required app version now and then, so the version header is a
  setting. The token lasts months and is cached in NVS.

  Copyright (C) 2026 Patrick Sonnerat
*/
#ifndef LIBRELINKUPCLIENT_H
#define LIBRELINKUPCLIENT_H

#include <Arduino.h>

void llTick();                 // polls when due (Wi-Fi up, source = LibreLinkUp)
void llRequestNow();
const char *llStatus();        // "" when fine, else a short error for the display
void llForgetSession();        // after a credential change

#endif
