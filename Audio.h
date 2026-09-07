/*
  Audio.h - ES8311 codec + I2S tone generator for the on-board speaker
  (part of WaveshareMon, GPL v3, see LICENSE)

  Copyright (C) 2026 Patrick Sonnerat
*/
#ifndef AUDIO_H
#define AUDIO_H

#include <Arduino.h>

class Audio {
public:
  bool begin();                                  // Wire must already be started
  // non-blocking; volume 0-100
  void tone(uint16_t freq, uint32_t durationMs, uint8_t volume);
  bool isPlaying() const;
  void mute();
  bool available() const { return enabled; }
private:
  bool enabled = false;
};

extern Audio audio;

#endif
