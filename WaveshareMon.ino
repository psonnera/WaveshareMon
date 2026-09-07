/*
  WaveshareMon - glucose monitor for the Waveshare ESP32-S3-ePaper-1.54G

  Shows the current glucose value, trend, delta and a 4-hour graph on the
  1.54" four-colour e-paper. Data comes either from xDrip over the Open
  Bluetooth Broadcast protocol (BLE, phone = GATT server) or from Nightscout
  over Wi-Fi. There are no navigation buttons: settings are written by the
  companion Android app (Android/xDripOBB) over BLE, the BOOT button snoozes
  alarms (short press) or toggles setup mode (long press).

  Copyright (C) 2026 Patrick Sonnerat
  Derived from M5Stack_xDripMon and M5_NightscoutMon (Martin Lukasek).

  This program is free software: you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the Free
  Software Foundation, either version 3 of the License, or (at your option)
  any later version. See LICENSE.
*/

#include "Version.h"
#include "Board.h"
#include "AppConfig.h"
#include "GlucoseState.h"
#include "Alarms.h"
#include "Audio.h"
#include "Battery.h"
#include "TimeService.h"
#include "BleObbClient.h"
#include "BleSetupServer.h"
#include "WifiService.h"
#include "NightscoutClient.h"
#include "EpdUi.h"
#include "DebugInject.h"
#include "Log.h"
#include <Wire.h>
#include <NimBLEDevice.h>

// setup advertising stays on this long after boot (or a long press)
#define SETUP_WINDOW_MS   (10UL * 60 * 1000)
#define LONG_PRESS_MS     3000

static uint32_t setupStartMs = 0;
static bool     setupTimed = false;

static void enterSetupMode(bool timed) {
  setupStartMs = millis();
  setupTimed = timed;
  setupServerAdvertise(true);
}

static void pollButton() {
  static bool     wasDown = false;
  static uint32_t downMs = 0;
  static bool     longDone = false;
  bool down = digitalRead(PIN_BOOT_BTN) == LOW;
  uint32_t now = millis();
  if (down && !wasDown) { downMs = now; longDone = false; }
  if (down && !longDone && now - downMs >= LONG_PRESS_MS) {
    longDone = true;
    if (setupServerAdvertising()) setupServerAdvertise(false);
    else enterSetupMode(true);
  }
  if (!down && wasDown && !longDone && now - downMs > 30) {
    alarms.snooze();
  }
  wasDown = down;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);

  cfg.load();
  battery.begin();
  gs.restore();
  timeService.begin();
  logAdd("boot v%s %s", WSMON_VERSION, cfg.name());
  if (cfg.firstRun) logAdd("no config: setup mode");

  ui.begin();
  audio.begin();

  NimBLEDevice::init(cfg.name());
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  // Just Works bonding with an encrypted link, as required by the OBB spec
  NimBLEDevice::setSecurityAuth(true /*bond*/, false /*mitm*/, true /*secure conn*/);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  NimBLEDevice::setMTU(517);

  setupServerBegin();
  // unconfigured devices advertise permanently, configured ones for 10 min
  enterSetupMode(!cfg.firstRun);

  wifiBegin();
  obbBegin();
}

void loop() {
  pollButton();
  debugInjectPoll();

  // one phone cannot hold a setup link and an OBB link at the same time
  obbSetPaused(setupServerClientConnected() || cfg.source != SRC_OBB);
  if (cfg.source == SRC_OBB) obbTick();

  wifiTick();
  nsTick();
  timeService.tick();
  battery.tick();
  alarms.tick();
  setupServerTick();

  if (setupTimed && setupServerAdvertising() && !setupServerClientConnected() &&
      millis() - setupStartMs > SETUP_WINDOW_MS) {
    setupServerAdvertise(false);
  }

  ui.tick();
  delay(10);
}
