/*
  NightscoutClient.h - Nightscout REST polling
  (part of WaveshareMon, GPL v3, see LICENSE; derived from M5_NightscoutMon)

  Copyright (C) 2026 Patrick Sonnerat
*/
#ifndef NIGHTSCOUTCLIENT_H
#define NIGHTSCOUTCLIENT_H

#include <Arduino.h>

void nsTick();                 // polls when due (aligned to the 5-minute cadence)
void nsRequestNow();           // force a fetch at the next tick
int  nsLastError();            // 0 = ok, HTTP code or negative HTTPClient error
uint32_t nsLastFetchMs();

#endif
