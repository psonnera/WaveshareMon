/*
  DexcomShareClient.h - Dexcom Share follower API polling
  (part of WaveshareMon, GPL v3, see LICENSE)

  Uses the same unofficial Share endpoints as pydexcom / share2nightscout-
  bridge / Loop: AuthenticatePublisherAccount, LoginPublisherAccountById and
  ReadPublisherLatestGlucoseValues. The publisher's own account is used and at
  least one follower must be set up in the Dexcom app for the service to
  return values. The session is cached in NVS and only renewed when the
  server rejects it; credential errors back off for 15 minutes so the
  account never gets locked by retries.

  Copyright (C) 2026 Patrick Sonnerat
*/
#ifndef DEXCOMSHARECLIENT_H
#define DEXCOMSHARECLIENT_H

#include <Arduino.h>

void dxTick();                 // polls when due (Wi-Fi up, source = Dexcom)
void dxRequestNow();
const char *dxStatus();        // "" when fine, else a short error for the display
void dxForgetSession();        // after a credential change

#endif
