/*
  EpdUi.h - 200x200 four-colour e-paper user interface
  (part of WaveshareMon, GPL v3, see LICENSE)

  Copyright (C) 2026 Patrick Sonnerat
*/
#ifndef EPDUI_H
#define EPDUI_H

#include <Arduino.h>

class EpdUi {
public:
  void begin();                   // panel init + splash
  void tick();                    // decides when a refresh is due (main loop)
  void requestRedraw() { redrawPending = true; }
  bool busy() const { return rendering; }
private:
  void render();                  // full-screen redraw (blocks ~20 s while the panel refreshes)
  void drawHeader();
  void drawValue();
  void drawTrendRow();
  void drawGraph(int y0, int h);
  void drawBottomBar();
  volatile bool redrawPending = false;
  volatile bool rendering = false;
  uint32_t lastRenderMs = 0;
  int      lastAgeShown = -1;
  bool     lastStale = false;
};

extern EpdUi ui;

#endif
