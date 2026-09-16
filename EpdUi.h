/*
  EpdUi.h - 200x200 four-colour e-paper user interface
  (part of WaveshareMon, GPL v3, see LICENSE)

  Copyright (C) 2026 Patrick Sonnerat
*/
#ifndef EPDUI_H
#define EPDUI_H

#include <Arduino.h>
#include <time.h>

class EpdUi {
public:
  void begin(bool splash);        // splash on a cold boot; after deep sleep the panel is left alone
  void tick();                    // decides when a refresh is due (awake loop only)
  void flush();                   // end of a wake window: render now if anything changed
  void powerDown();               // panel supply off before deep sleep
  void requestRedraw() { redrawPending = true; }
  bool busy() const { return rendering; }
  bool panelOk() const { return !panelMismatch; }   // false: the BUSY line says this is the other panel
private:
  bool due();                     // consumes the change flags
  void ensureInit();              // panel supply + SPI + driver init, once per wake
  void render();                  // full-screen redraw (blocks ~20 s while the panel refreshes)
  void drawStatusPage();          // shown until the configured source delivers a reading
  void drawHeader();
  void drawValue();
  void drawTrendRow();
  void drawGraph(int y0, int h);
  bool drawSideText();            // info line beside the graph; false when it did not fit
  void drawBottomBar(bool infoShown);
  uint16_t bandFg = 0;            // text colour inside the highlight band (set by drawValue)
  volatile bool redrawPending = false;
  volatile bool rendering = false;
  bool     inited = false;
  bool     panelMismatch = false;
};

extern EpdUi ui;

#endif
