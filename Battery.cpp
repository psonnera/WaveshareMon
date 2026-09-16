/*
  Battery.cpp - LiPo voltage via ADC
  (part of WaveshareMon, GPL v3, see LICENSE)

  Copyright (C) 2026 Patrick Sonnerat
*/
#include "Battery.h"
#include "Board.h"
#include "BoardPower.h"

Battery battery;

void Battery::begin() {
  boardBatteryLatch(true);              // keep running from the battery after USB unplug
  analogSetPinAttenuation(PIN_BAT_ADC, ADC_11db);
  lastMs = 0;
  tick();
}

void Battery::tick() {
  if (lastMs && millis() - lastMs < 10000) return;
  lastMs = millis();
  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) sum += analogReadMilliVolts(PIN_BAT_ADC);
  mv = (int)(sum / 8) * 2;
}

int Battery::percent() const {
  if (mv < 2500) return -1;                    // nothing connected
  // simple LiPo discharge curve approximation
  static const struct { int mv; int pct; } tab[] = {
    {4200, 100}, {4100, 90}, {4000, 78}, {3900, 62}, {3800, 45},
    {3700, 25}, {3600, 12}, {3500, 5}, {3300, 0}
  };
  if (mv >= tab[0].mv) return 100;
  for (unsigned i = 1; i < sizeof(tab) / sizeof(tab[0]); i++) {
    if (mv >= tab[i].mv) {
      int span = tab[i - 1].mv - tab[i].mv;
      return tab[i].pct + (mv - tab[i].mv) * (tab[i - 1].pct - tab[i].pct) / span;
    }
  }
  return 0;
}
