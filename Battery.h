/*
  Battery.h - LiPo voltage via ADC (GPIO4, 1:2 divider)
  (part of WaveshareMon, GPL v3, see LICENSE)

  Copyright (C) 2026 Patrick Sonnerat
*/
#ifndef BATTERY_H
#define BATTERY_H

#include <Arduino.h>

class Battery {
public:
  void begin();
  void tick();                       // samples every 10 s
  int  millivolts() const { return mv; }
  int  percent() const;              // -1 = no battery detected
  bool onUsb() const { return mv > 4350 || mv < 2500; }   // charger holds VBAT high / no cell
private:
  int mv = 0;
  uint32_t lastMs = 0;
};

extern Battery battery;

#endif
