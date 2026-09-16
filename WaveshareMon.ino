/*
  WaveshareMon - glucose monitor for the Waveshare ESP32-S3-ePaper-1.54G

  Shows the current glucose value, trend, delta and a 4-hour graph on the
  1.54" four-colour e-paper. Data comes either from xDrip over the Open
  Bluetooth Broadcast protocol (BLE, phone app = GATT server), from xDrip's
  Mi Band support (BLE, the board poses as a Mi Band 2), or over Wi-Fi from
  Nightscout, Dexcom Share or LibreLinkUp. There are no navigation buttons:
  settings are written by the
  companion Android app over BLE, the BOOT button snoozes alarms (short press)
  or toggles setup mode (long press), the PWR button fetches now (short press)
  or powers the device off (held 2 s).

  A configured device deep-sleeps between readings (see PowerCycle.h); the
  always-on loop runs after a cold boot, on the first (unconfigured) run, in
  setup mode and with the "nosleep" debug flag.

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
#include "BleMiBand.h"
#include "BleXdrip4iOS.h"
#include "BleSetupServer.h"
#include "WifiService.h"
#include "NightscoutClient.h"
#include "DexcomShareClient.h"
#include "LibreLinkUpClient.h"
#include "EpdUi.h"
#include "PowerCycle.h"
#include "OtaUpdate.h"
#include "WebSetup.h"
#include "BoardPower.h"
#include "DebugInject.h"
#include "Log.h"
#include <Wire.h>
#include <NimBLEDevice.h>
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <esp_sleep.h>
#include <driver/gpio.h>

#define LONG_PRESS_MS     3000
#define PWR_OFF_MS        2000                 // hold PWR this long to power off
#define WDT_TIMEOUT_S     60                   // reboot if loop() stalls this long

static bool s_bleUp = false;

// Bluetooth is brought up only when the source needs it or the setup service
// must be reachable; Wi-Fi sources otherwise leave the radio off entirely.
static void bleBegin() {
  if (s_bleUp) return;
  s_bleUp = true;
  // xDrip's Mi Band support reads the GAP device name and expects "MI Band 2";
  // xDrip4iOS looks for a name containing "M5Stack"
  NimBLEDevice::init(cfg.source == SRC_MIBAND    ? "MI Band 2" :
                     cfg.source == SRC_XDRIP4IOS ? xdrip4iosName() : cfg.name());
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  if (cfg.source == SRC_MIBAND || cfg.source == SRC_XDRIP4IOS) {
    // Phones cache the GATT table of a bonded address, and the OBB/setup-only
    // table of the public address would hide the Mi Band services from xDrip.
    // The Mi Band and xDrip4iOS identities therefore get their own stable
    // static random address, derived from the public one (xDrip4iOS remembers
    // the device by that address after the first connection).
    uint8_t v[6];
    memcpy(v, NimBLEDevice::getAddress().getBase()->val, 6);
    v[5] |= 0xC0;                       // static random address marker
    NimBLEDevice::setOwnAddr(v);
    NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);
  }
  // Just Works bonding with an encrypted link, as required by the OBB spec
  NimBLEDevice::setSecurityAuth(true /*bond*/, false /*mitm*/, cfg.bleSecureConn != 0 /*secure conn*/);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  NimBLEDevice::setMTU(517);
  setupServerBegin();
  if (cfg.source == SRC_OBB) obbBegin();
  if (cfg.source == SRC_MIBAND) miBandBegin();
  if (cfg.source == SRC_XDRIP4IOS) xdrip4iosBegin();
}

void enterSetupMode(bool timed) {   // also used by the serial "setup" command
  bleBegin();
  setupServerAdvertise(true);
  if (timed) cycleStayAwake(SETUP_WINDOW_MS);
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
    if (cfg.source == SRC_MIBAND) miBandSendSnooze();
  }
  wasDown = down;
}

// Power off: the e-paper keeps its image, so the screen does not change. Deep
// sleep drops the CPU to a few uA and releases the USB, so this is the "off"
// state. Unlike the power-cycle sleep the battery latch is NOT held, so on
// battery the board really powers down; a PWR press re-latches it and boots.
// On USB it wakes through the ext0 source below.
void powerOff() {
  logAdd("power off (PWR to wake)");
  Serial.println("[dbg] powering off - press PWR to wake");
  Serial.flush();
  delay(50);
  ui.powerDown();
  audio.powerDown();
  boardPowerOff();                          // latch released, PWR (pulled low) wakes
  esp_deep_sleep_start();
}

// Hold PWR ~2 s to power off. A short press only wakes the device (fetch now).
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

// A button press woke the chip: decide between a short and a long press
// before the radios start, so the response is immediate.
static void handleWakeButton() {
  WakeKind k = cycleWakeKind();
  if (k != WAKE_BUTTON_BOOT && k != WAKE_BUTTON_PWR) return;
  int pin = k == WAKE_BUTTON_PWR ? PIN_PWR_BTN : PIN_BOOT_BTN;
  uint32_t limit = k == WAKE_BUTTON_PWR ? PWR_OFF_MS : LONG_PRESS_MS;
  uint32_t t0 = millis();
  while (digitalRead(pin) == LOW && millis() - t0 < limit) delay(5);
  bool held = digitalRead(pin) == LOW;
  if (k == WAKE_BUTTON_PWR) {
    if (held) powerOff();
    logAdd("PWR: fetch now");                 // short press: a normal wake window follows
  } else if (held) {
    logAdd("BOOT held: setup mode");
    enterSetupMode(true);
    while (digitalRead(pin) == LOW) delay(5);
  } else {
    alarms.snooze();                          // short press
  }
}

void setup() {
  cycleBegin();                               // wake cause, power rails, GPIO holds
  bool cold = cycleWakeKind() == WAKE_COLD;
  Serial.begin(115200);
  if (cold) delay(200);
#if CORE_DEBUG_LEVEL >= 4
  esp_log_level_set("*", ESP_LOG_DEBUG);          // NimBLE host / Wi-Fi traces (debug builds only)
#endif
  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);
  pinMode(PIN_PWR_BTN, INPUT_PULLUP);
  // (the I2C bus is started by cycleBegin() -> boardPowerBegin())

  // Reboot the board if the main loop ever stalls for WDT_TIMEOUT_S, so a hung
  // firmware recovers on its own instead of needing the battery pulled. The
  // e-paper refresh blocks the loop for ~20 s, well under the timeout.
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  // core 3.x (IDF 5): the watchdog already runs with the IDF default; reconfigure it
  esp_task_wdt_config_t wdt = {};
  wdt.timeout_ms = WDT_TIMEOUT_S * 1000;
  wdt.idle_core_mask = 0;
  wdt.trigger_panic = true;                   // reset on timeout
  if (esp_task_wdt_reconfigure(&wdt) != ESP_OK) esp_task_wdt_init(&wdt);
#else
  esp_task_wdt_init(WDT_TIMEOUT_S, true /* reset on timeout */);
#endif
  esp_task_wdt_add(NULL);

  cfg.load();
  // the USB serial re-enumerates after every wake; give the host a moment
  // when someone is watching the console
  if (!cold && cfg.debugLog) delay(1500);
  battery.begin();
  gs.restore();
  alarms.restore();
  timeService.begin();
  if (cold) logAdd("boot v%s %s (%s)", WSMON_VERSION, cfg.name(), BOARD_NAME);
  else      logAdd("wake %lu (%s)", (unsigned long)cycleWakes(), cycleWakeName());
  if (cfg.firstRun) logAdd("no config: setup mode");

  ui.begin(cold);                             // splash only on a cold boot
  handleWakeButton();

  if (SRC_IS_BLE(cfg.source) || cycleAwake()) bleBegin();
  // cold boot: setup advertising for 10 min (permanently while unconfigured)
  if (cold) enterSetupMode(!cfg.firstRun);

  wifiBegin();
}

void loop() {
  esp_task_wdt_reset();
  pollButton();
  pollPwrButton();
  debugInjectPoll();

  // one phone cannot hold a setup link and an OBB link at the same time
  obbSetPaused(setupServerClientConnected() || cfg.source != SRC_OBB);
  if (cfg.source == SRC_OBB) obbTick();
  if (cfg.source == SRC_MIBAND) miBandTick();
  if (cfg.source == SRC_XDRIP4IOS) xdrip4iosTick();

  wifiTick();
  nsTick();
  dxTick();
  llTick();
  timeService.tick();
  battery.tick();
  // sleeping modes evaluate the alarms once, after the fetch (PowerCycle)
  if (cycleAwake()) alarms.tick();
  setupServerTick();
  webSetupTick();                             // Wi-Fi setup page + access point while in setup mode
  otaTick();                                  // firmware update check / install (blocks while installing)

  cycleTick();                                // may deep-sleep and not return
  ui.tick();
  delay(10);
}
