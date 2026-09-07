/*
  EpdUi.cpp - 200x200 four-colour e-paper user interface
  (part of WaveshareMon, GPL v3, see LICENSE)

  Layout and drawing helpers (icons, trend arrow, mini graph, colour rules)
  derive from M5_NightscoutMon (Martin Lukasek, GPL v3) via M5Stack_xDripMon.
  Panel driver: GxEPD2 (Jean-Marc Zingg), class GxEPD2_154c_GDEM0154F51H,
  whose init sequence matches Waveshare's EPD_1in54g driver.

  Copyright (C) 2026 Patrick Sonnerat
*/
#include "EpdUi.h"
#include "Board.h"
#include "AppConfig.h"
#include "GlucoseState.h"
#include "Alarms.h"
#include "Battery.h"
#include "TimeService.h"
#include "BleObbClient.h"
#include "BleSetupServer.h"
#include "WifiService.h"
#include "Version.h"
#include <SPI.h>
#include <GxEPD2_4C.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>

extern const unsigned char bat0_icon16x16[], bat1_icon16x16[], bat2_icon16x16[],
                           bat3_icon16x16[], bat4_icon16x16[], plug_icon16x16[],
                           bluetooth_icon16x16[], wifi2_icon16x16[], clock_icon16x16[],
                           warning_icon16x16[];

EpdUi ui;

#define W 200
#define H 200

static GxEPD2_4C<GxEPD2_154c_GDEM0154F51H, GxEPD2_154c_GDEM0154F51H::HEIGHT>
  display(GxEPD2_154c_GDEM0154F51H(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY));

// ---- primitives ----------------------------------------------------------------

static void drawIcon(int16_t x, int16_t y, const unsigned char *bitmap, uint16_t color) {
  display.drawBitmap(x, y, bitmap, 16, 16, color);
}

enum Align { AL_LEFT, AL_CENTER, AL_RIGHT };

// draw text with its top-left / top-center / top-right at (x, y)
static int16_t drawText(const char *s, int16_t x, int16_t y, const GFXfont *font, uint8_t size,
                        uint16_t color, Align al = AL_LEFT) {
  int16_t bx, by; uint16_t bw, bh;
  display.setFont(font);
  display.setTextSize(size);
  display.setTextColor(color);
  display.getTextBounds(s, 0, 0, &bx, &by, &bw, &bh);
  int16_t x0 = x;
  if (al == AL_CENTER) x0 = x - bw / 2;
  else if (al == AL_RIGHT) x0 = x - bw;
  display.setCursor(x0 - bx, y - by);
  display.print(s);
  return bw;
}

static void textSize(const char *s, const GFXfont *font, uint8_t size, uint16_t &w, uint16_t &h) {
  int16_t bx, by;
  display.setFont(font);
  display.setTextSize(size);
  display.getTextBounds(s, 0, 0, &bx, &by, &w, &h);
}

// NightscoutMon trend arrow: angle -90 (up) .. 90 (down), 0 = flat
static void drawArrow(int x, int y, int asize, int aangle, int pwidth, int plength, uint16_t color) {
  float dx = (asize - 10) * cos(aangle - 90) * PI / 180 + x;
  float dy = (asize - 10) * sin(aangle - 90) * PI / 180 + y;
  float x1 = 0;           float y1 = plength;
  float x2 = pwidth / 2;  float y2 = pwidth / 2;
  float x3 = -pwidth / 2; float y3 = pwidth / 2;
  float angle = aangle * PI / 180 - 135;
  float xx1 = x1 * cos(angle) - y1 * sin(angle) + dx;
  float yy1 = y1 * cos(angle) + x1 * sin(angle) + dy;
  float xx2 = x2 * cos(angle) - y2 * sin(angle) + dx;
  float yy2 = y2 * cos(angle) + x2 * sin(angle) + dy;
  float xx3 = x3 * cos(angle) - y3 * sin(angle) + dx;
  float yy3 = y3 * cos(angle) + x3 * sin(angle) + dy;
  display.fillTriangle(xx1, yy1, xx3, yy3, xx2, yy2, color);
  for (int o = -2; o <= 2; o++) {
    display.drawLine(x + o, y, xx1 + o, yy1, color);
    display.drawLine(x, y + o, xx1, yy1 + o, color);
  }
}

// colour band for the current value: 0 normal, 1 yellow warning, 2 red alarm
static int valueLevel() {
  if (!gs.hasData) return 0;
  if (gs.mgdl < cfg.redLow || gs.mgdl > cfg.redHigh) return 2;
  if (gs.mgdl < cfg.yellowLow || gs.mgdl > cfg.yellowHigh) return 1;
  return 0;
}

// ---- screen parts ---------------------------------------------------------------

void EpdUi::drawHeader() {
  // reading time (left), link + battery icons (right)
  char t[12] = "";
  if (gs.hasData && gs.readingUtc) timeService.formatTime(gs.readingUtc, t, sizeof(t));
  if (t[0]) drawText(t, 2, 2, &FreeSansBold9pt7b, 1, GxEPD_BLACK);

  int x = W - 18;
  int pct = battery.percent();
  if (battery.onUsb() && pct < 0) drawIcon(x, 1, plug_icon16x16, GxEPD_BLACK);
  else if (pct >= 0) {
    const unsigned char *ic = pct <= 12 ? bat0_icon16x16 : pct <= 40 ? bat1_icon16x16 :
                              pct <= 65 ? bat2_icon16x16 : pct <= 90 ? bat3_icon16x16 : bat4_icon16x16;
    drawIcon(x, 1, ic, pct <= 12 ? GxEPD_RED : GxEPD_BLACK);
  }
  x -= 20;
  if (cfg.source == SRC_OBB) {
    ObbState s = obbState();
    uint16_t c = s == OBB_CONNECTED ? GxEPD_BLACK : GxEPD_RED;
    drawIcon(x, 1, bluetooth_icon16x16, c);
  } else {
    drawIcon(x, 1, wifi2_icon16x16, wifiConnected() ? GxEPD_BLACK : GxEPD_RED);
  }
  x -= 20;
  if (alarms.isSnoozed()) drawIcon(x, 1, clock_icon16x16, GxEPD_RED);
}

void EpdUi::drawValue() {
  char v[12];
  gs.valueString(v, sizeof(v), cfg.isMgdl());
  const GFXfont *font = strlen(v) >= 4 ? &FreeSansBold18pt7b : &FreeSansBold24pt7b;
  uint16_t tw, th;
  textSize(v, font, 2, tw, th);
  int level = valueLevel();
  bool stale = gs.hasData && (gs.isStale() || !gs.live);
  const int top = 24, bandH = 76;
  uint16_t fg = GxEPD_BLACK;
  if (level == 2) { display.fillRoundRect(2, top, W - 4, bandH, 8, GxEPD_RED); fg = GxEPD_WHITE; }
  else if (level == 1) { display.fillRoundRect(2, top, W - 4, bandH, 8, GxEPD_YELLOW); }
  int ty = top + (bandH - th) / 2;
  drawText(v, W / 2, ty, font, 2, fg, AL_CENTER);
  if (stale) {
    // strike-through: the value can no longer be trusted
    int y = ty + th / 2;
    display.fillRect(W / 2 - tw / 2 - 6, y - 2, tw + 12, 5, level == 2 ? GxEPD_WHITE : GxEPD_RED);
  }
}

void EpdUi::drawTrendRow() {
  const int y = 104, h = 36;
  // arrow (left)
  if (gs.hasData && gs.arrowAngle != ARROW_HIDDEN && !gs.isStale())
    drawArrow(22, y + h / 2, 12, gs.arrowAngle + 85, 14, 24, GxEPD_BLACK);
  // delta (centre)
  char d[12];
  gs.deltaString(d, sizeof(d), cfg.isMgdl());
  drawText(d, W / 2, y + 6, &FreeSansBold12pt7b, 1, GxEPD_BLACK, AL_CENTER);
  // age (right)
  char age[16];
  if (!gs.hasData) age[0] = 0;
  else {
    int m = gs.minutesAgo();
    if (m < 60) snprintf(age, sizeof(age), "%d min", m);
    else if (m < 1440) snprintf(age, sizeof(age), "%dh%02d", m / 60, m % 60);
    else strlcpy(age, "old", sizeof(age));
  }
  drawText(age, W - 4, y + 10, &FreeSans9pt7b, 1, gs.minutesAgo() > 15 ? GxEPD_RED : GxEPD_BLACK, AL_RIGHT);
}

void EpdUi::drawGraph(int y0, int hgt) {
  // up to 48 points (4 h), 4 px pitch, clamped 40..300 mg/dL
  const int x0 = 4, pitch = 4;
  const float lo = 40.0f, hi = 300.0f;
  auto yOf = [&](float mg) {
    if (mg > hi) mg = hi; if (mg < lo) mg = lo;
    return (int)(y0 + hgt - 1 - (mg - lo) * (hgt - 1) / (hi - lo));
  };
  display.drawFastHLine(x0, yOf(cfg.yellowHigh), W - 8, GxEPD_YELLOW);
  display.drawFastHLine(x0, yOf(cfg.yellowLow), W - 8, GxEPD_YELLOW);
  display.drawFastHLine(x0, yOf(cfg.redHigh), W - 8, GxEPD_RED);
  display.drawFastHLine(x0, yOf(cfg.redLow), W - 8, GxEPD_RED);
  int n = gs.histCount;
  for (int i = 0; i < n; i++) {
    uint16_t v = gs.hist[i];
    if (v == 0) continue;
    int x = x0 + (HIST_SIZE - n + i) * pitch;
    uint16_t c = GxEPD_BLACK;
    if (v < cfg.redLow || v > cfg.redHigh) c = GxEPD_RED;
    display.fillCircle(x, yOf(v), 1, c);
    display.drawPixel(x, yOf(v) - 1, c);
  }
}

void EpdUi::drawBottomBar() {
  const int y = H - 22;
  const char *alarm = alarms.label();
  if (alarm[0]) {
    AlarmState s = alarms.state();
    bool red = s == ALARM_ALARM_LOW || s == ALARM_ALARM_HIGH || s == ALARM_REMOTE;
    display.fillRect(0, y, W, 22, red ? GxEPD_RED : GxEPD_YELLOW);
    char txt[40];
    int sn = alarms.snoozeRemainingMin();
    if (sn) snprintf(txt, sizeof(txt), "%s  zz %d'", alarm, sn);
    else strlcpy(txt, alarm, sizeof(txt));
    drawText(txt, W / 2, y + 4, &FreeSansBold9pt7b, 1, red ? GxEPD_WHITE : GxEPD_BLACK, AL_CENTER);
    return;
  }
  // status text: setup mode > connection state > info line
  char txt[96];
  if (setupServerAdvertising())
    snprintf(txt, sizeof(txt), "Setup: %s", cfg.name());
  else if (cfg.source == SRC_OBB && obbState() != OBB_CONNECTED)
    snprintf(txt, sizeof(txt), "xDrip: %s", obbStateName());
  else if (cfg.source == SRC_NIGHTSCOUT && !wifiConnected())
    snprintf(txt, sizeof(txt), "Wi-Fi: %s", wifiStateName());
  else if (gs.infoLine[0])
    strlcpy(txt, gs.infoLine, sizeof(txt));
  else
    txt[0] = 0;
  if (txt[0]) {
    display.drawFastHLine(0, y, W, GxEPD_BLACK);
    drawText(txt, W / 2, y + 5, &FreeSans9pt7b, 1, GxEPD_BLACK, AL_CENTER);
  }
}

// ---- rendering ------------------------------------------------------------------

void EpdUi::render() {
  rendering = true;
  display.setFullWindow();
  display.setRotation(0);
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawHeader();
    drawValue();
    drawTrendRow();
    drawGraph(142, 34);
    drawBottomBar();
  } while (display.nextPage());
  display.hibernate();
  lastRenderMs = millis();
  lastAgeShown = gs.hasData ? gs.minutesAgo() : -1;
  lastStale = gs.hasData && gs.isStale();
  rendering = false;
}

void EpdUi::begin() {
  pinMode(PIN_EPD_PWR, OUTPUT);
  digitalWrite(PIN_EPD_PWR, LOW);         // panel supply on
  delay(20);
  SPI.begin(PIN_EPD_SCK, -1, PIN_EPD_MOSI, PIN_EPD_CS);
  display.init(0, true, 2, false);
  display.setRotation(0);
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawText("WaveshareMon", W / 2, 60, &FreeSansBold12pt7b, 1, GxEPD_BLACK, AL_CENTER);
    drawText("v" WSMON_VERSION, W / 2, 90, &FreeSans9pt7b, 1, GxEPD_BLACK, AL_CENTER);
    drawText(cfg.name(), W / 2, 130, &FreeSans9pt7b, 1, GxEPD_RED, AL_CENTER);
  } while (display.nextPage());
  display.hibernate();
  lastRenderMs = millis();
  redrawPending = true;                   // first data screen after boot
}

void EpdUi::tick() {
  if (rendering) return;
  uint32_t now = millis();
  bool due = redrawPending;
  if (gs.dataChanged)       { gs.dataChanged = false; due = true; }
  if (alarms.stateChanged)  { alarms.stateChanged = false; due = true; }
  if (obbStateChanged)      { obbStateChanged = false; due = true; }
  if (wifiStateChanged)     { wifiStateChanged = false; due = true; }
  // age display: refresh at most every 5 minutes, or on the stale transition
  if (gs.hasData) {
    int age = gs.minutesAgo();
    bool stale = gs.isStale();
    if (stale != lastStale) due = true;
    else if (age != lastAgeShown && now - lastRenderMs > 5UL * 60000UL) due = true;
  }
  if (!due) return;
  // the panel needs ~20 s per refresh; never refresh more often than every 25 s
  if (now - lastRenderMs < 25000UL) { redrawPending = true; return; }
  redrawPending = false;
  render();
}
