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
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <esp_sleep.h>

// setup advertising stays on this long after boot (or a long press)
#define SETUP_WINDOW_MS   (10UL * 60 * 1000)
#define LONG_PRESS_MS     3000
#define PWR_OFF_MS        2000                 // hold PWR this long to power off
#define WDT_TIMEOUT_S     60                   // reboot if loop() stalls this long

static uint32_t setupStartMs = 0;
static bool     setupTimed = false;

void enterSetupMode(bool timed) {   // also used by the serial "setup" command
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

// Power off: the e-paper keeps its image, so the screen does not change. Deep
// sleep drops the CPU to a few uA (battery lasts months) and, more usefully,
// releases the USB, so this is the "off" state. A PWR press wakes it: on
// battery the hardware power path re-latches and it boots; on USB it wakes
// through the ext0 source below. Nothing here forces the battery latch low, so
// a wrong guess about that circuit cannot brick the device.
void powerOff() {
  logAdd("power off (PWR to wake)");
  Serial.println("[dbg] powering off - press PWR to wake");
  Serial.flush();
  delay(50);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_PWR_BTN, 0);   // wake when PWR pulled low
  esp_deep_sleep_start();
}

// Hold PWR ~2 s to power off. A short press does nothing (avoids accidents).
static void pollPwrButton() {
  static bool     wasDown = false;
  static uint32_t downMs = 0;
  static bool     done = false;
  bool down = digitalRead(PIN_PWR_BTN) == LOW;
  uint32_t now = millis();
  if (down && !wasDown) { downMs = now; done = false; }
  if (down && !done && now - downMs >= PWR_OFF_MS) { done = true; powerOff(); }
  wasDown = down;
}

void setup() {
  Serial.begin(115200);
  delay(200);
#if CORE_DEBUG_LEVEL >= 4
  esp_log_level_set("*", ESP_LOG_DEBUG);          // NimBLE host / Wi-Fi traces (debug builds only)
#endif
  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);
  pinMode(PIN_PWR_BTN, INPUT_PULLUP);
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);

  // Reboot the board if the main loop ever stalls for WDT_TIMEOUT_S, so a hung
  // firmware recovers on its own instead of needing the battery pulled. The
  // e-paper refresh blocks the loop for ~20 s, well under the timeout.
  esp_task_wdt_init(WDT_TIMEOUT_S, true /* reset on timeout */);
  esp_task_wdt_add(NULL);

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
  NimBLEDevice::setSecurityAuth(true /*bond*/, false /*mitm*/, cfg.bleSecureConn != 0 /*secure conn*/);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  NimBLEDevice::setMTU(517);

  setupServerBegin();
  // unconfigured devices advertise permanently, configured ones for 10 min
  enterSetupMode(!cfg.firstRun);

  wifiBegin();
  obbBegin();
}

void loop() {
  esp_task_wdt_reset();
  pollButton();
  pollPwrButton();
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
