/*
  PowerCycle.cpp - wake classification, radio window and deep-sleep scheduling
  (part of WaveshareMon, GPL v3, see LICENSE)

  Copyright (C) 2026 Patrick Sonnerat
*/
#include <esp_attr.h>
#include "PowerCycle.h"
#include "AppConfig.h"
#include "Board.h"
#include "GlucoseState.h"
#include "Alarms.h"
#include "Audio.h"
#include "BleObbClient.h"
#include "BleMiBand.h"
#include "BleXdrip4iOS.h"
#include "BoardPower.h"
#include "BleSetupServer.h"
#include "WifiService.h"
#include "NightscoutClient.h"
#include "DexcomShareClient.h"
#include "LibreLinkUpClient.h"
#include "EpdUi.h"
#include "Log.h"
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>

#define READING_PERIOD_S  300
#define POLL_LEAD_S       15        // wake this long after the expected reading (polling sources)
#define PUSH_LEAD_S       -12       // ...or this long BEFORE it, to be advertising when xDrip pushes
#define WINDOW_BLE_MS     30000UL   // radio window: scan + connect + first notification
#define WINDOW_WIFI_MS    40000UL   // join (fast path, then a full scan) + HTTPS fetch
#define WINDOW_PUSH_MS    50000UL   // Mi Band: xDrip connects a few seconds after its reading
#define RETRY_S           60        // nothing fetched: look again soon...
#define RETRY_MAX         5         // ...this many times (covers the grace before the value is crossed out), then back to the 5-minute grid
#define MIN_SLEEP_S       20
#define MAX_SLEEP_S       3600

struct CycleRtc {
  uint32_t magic;
  uint32_t wakes;
  uint8_t  failStreak;
  char     status[32];              // bottom-bar status at the last render
};
#define CYCLE_MAGIC 0x43594331UL
RTC_DATA_ATTR static CycleRtc s_rtc;

static WakeKind s_kind = WAKE_COLD;
static uint32_t s_wakeMs = 0;
static uint32_t s_awakeUntilMs = 0;     // timed awake hold, 0 = none
static uint32_t s_seqAtWake = 0;
static uint32_t s_readingsAtWake = 0;
static bool     s_sleepNow = false;
static char     s_status[32] = "";

// ---- wake ---------------------------------------------------------------------

void cycleBegin() {
  // battery latch kept, rails on, sleep holds released, wake pins back to
  // GPIO - per board (GPIO holds on the S3, an I2C expander on the C6)
  boardPowerBegin();

  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  if (cause == ESP_SLEEP_WAKEUP_TIMER) {
    s_kind = WAKE_TIMER;
  } else if (cause == ESP_SLEEP_WAKEUP_EXT1) {
    s_kind = boardWakeWasPwr() ? WAKE_BUTTON_PWR : WAKE_BUTTON_BOOT;
  } else {
    s_kind = WAKE_COLD;
  }
  if (s_kind == WAKE_COLD || s_rtc.magic != CYCLE_MAGIC) {
    memset(&s_rtc, 0, sizeof(s_rtc));
    s_rtc.magic = CYCLE_MAGIC;
  }
  s_rtc.wakes++;
  s_wakeMs = millis();
  strlcpy(s_status, s_rtc.status, sizeof(s_status));
}

// called on the first cycleTick(), once the glucose state is restored
static void latchCounters() {
  s_seqAtWake = gs.sourceSeq;
  s_readingsAtWake = gs.readingSeq;
}

WakeKind cycleWakeKind() { return s_kind; }
uint32_t cycleWakes() { return s_rtc.wakes; }
const char *cycleWakeName() {
  switch (s_kind) {
    case WAKE_TIMER:       return "timer";
    case WAKE_BUTTON_BOOT: return "BOOT";
    case WAKE_BUTTON_PWR:  return "PWR";
    default:               return "boot";
  }
}

void cycleStayAwake(uint32_t ms) {
  uint32_t until = millis() + ms;
  if (!s_awakeUntilMs || (int32_t)(until - s_awakeUntilMs) > 0) s_awakeUntilMs = until;
}

bool cycleAwake() {
  if (cfg.noSleep || cfg.firstRun) return true;
  if (setupServerAdvertising()) return true;
  // a connected BLE client holds the device awake only in setup mode; in the
  // cycle xDrip (Mi Band) is a client too, and the setup app extends the hold
  // itself whenever it reads or writes something
  return s_awakeUntilMs && (int32_t)(s_awakeUntilMs - millis()) > 0;
}

void cycleSleepNow() { s_sleepNow = true; s_awakeUntilMs = 0; }
const char *cycleStatusText() { return s_status; }

// ---- sleep --------------------------------------------------------------------

static int32_t computeSleepS(bool contacted, bool newReading) {
  time_t now = time(nullptr);
  int32_t s = READING_PERIOD_S;
  bool clock = now > 1600000000 && gs.hasData && gs.readingUtc != 0;
  if (clock) {
    long age = (long)difftime(now, gs.readingUtc);
    if (contacted && !newReading && age > READING_PERIOD_S + 30 && s_rtc.failStreak < RETRY_MAX) {
      // the source answered but has nothing newer yet: look again soon
      s_rtc.failStreak++;
      s = RETRY_S;
    } else {
      int lead = (cfg.source == SRC_MIBAND || cfg.source == SRC_XDRIP4IOS) ? PUSH_LEAD_S : POLL_LEAD_S;
      time_t next = gs.readingUtc + READING_PERIOD_S + lead;
      while ((long)difftime(next, now) < MIN_SLEEP_S) next += READING_PERIOD_S;
      s = (int32_t)difftime(next, now);
    }
  }
  if (!contacted) {
    // no source at all this wake: a few quick retries, then the normal period
    if (s_rtc.failStreak < RETRY_MAX) { s_rtc.failStreak++; if (s > RETRY_S) s = RETRY_S; }
    else if (!clock) s = READING_PERIOD_S;
  }
  if (newReading) s_rtc.failStreak = 0;

  time_t alarmAt = alarms.nextWakeUtc(now);
  if (alarmAt) {
    long d = (long)difftime(alarmAt, now);
    if (d < MIN_SLEEP_S) d = MIN_SLEEP_S;
    if (d < s) s = (int32_t)d;
  }
  if (s < MIN_SLEEP_S) s = MIN_SLEEP_S;
  if (s > MAX_SLEEP_S) s = MAX_SLEEP_S;
  return s;
}

static void statusForFailure(char *out, size_t len) {
  if (cfg.source == SRC_OBB) {
    strlcpy(out, "xDrip: not found", len);
  } else if (cfg.source == SRC_MIBAND) {
    strlcpy(out, cfg.mibandKeySet ? "xDrip: no reading" : "xDrip: not paired", len);
  } else if (cfg.source == SRC_XDRIP4IOS) {
    strlcpy(out, cfg.x4iPassword[0] ? "xDrip4iOS: no reading" : "xDrip4iOS: not paired", len);
  } else if (!wifiConnected()) {
    snprintf(out, len, "Wi-Fi: %s", wifiFailText()[0] ? wifiFailText() : "no link");
  } else if (cfg.source == SRC_DEXCOM) {
    strlcpy(out, dxStatus()[0] ? dxStatus() : "Dexcom: no data", len);
  } else if (cfg.source == SRC_LIBRE) {
    strlcpy(out, llStatus()[0] ? llStatus() : "Libre: no data", len);
  } else {
    int e = nsLastError();
    if (e > 0 && e < 1000) snprintf(out, len, "Nightscout: HTTP %d", e);
    else if (e < 0)        strlcpy(out, "Nightscout: no reply", len);
    else                   strlcpy(out, "Nightscout: no data", len);
  }
}

void cycleSourceStatus(char *out, size_t len) {
  if (s_status[0]) { strlcpy(out, s_status, len); return; }     // last wake failed: say why
  switch (cfg.source) {
    case SRC_OBB:
      snprintf(out, len, "xDrip via phone: %s", obbStateName());
      break;
    case SRC_MIBAND:
      snprintf(out, len, "xDrip Mi Band: %s", miBandStateName());
      break;
    case SRC_XDRIP4IOS:
      snprintf(out, len, "xDrip4iOS: %s", xdrip4iosStateName());
      break;
    default:
      if (!cfg.wifiConfigured())        strlcpy(out, "Wi-Fi: not configured", len);
      else if (!wifiConnected())        snprintf(out, len, "Wi-Fi: %s", wifiFailText()[0] ? wifiFailText() : wifiStateName());
      else if (cfg.source == SRC_DEXCOM) strlcpy(out, dxStatus()[0] ? dxStatus() : (cfg.dxConfigured() ? "Dexcom: polling" : "Dexcom: no account"), len);
      else if (cfg.source == SRC_LIBRE)  strlcpy(out, llStatus()[0] ? llStatus() : (cfg.llConfigured() ? "Libre: polling" : "Libre: no account"), len);
      else if (!cfg.nsConfigured())     strlcpy(out, "Nightscout: no URL", len);
      else if (nsLastError())           snprintf(out, len, "Nightscout: error %d", nsLastError());
      else                              strlcpy(out, "Nightscout: polling", len);
      break;
  }
}

static void enterDeepSleep(int32_t seconds) {
  Serial.flush();
  delay(120);                       // let the USB host read the last log line
  // panel hibernated, amplifier idle, rails and battery latch kept and held
  // through sleep (see cycleBegin for why the rails stay on)
  ui.powerDown();
  audio.powerDown();
  // switches held, the buttons (active low) wake the chip, plus the timer
  boardPrepareSleep(false);
  esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
  esp_deep_sleep_start();
}

static void finishAndSleep(bool contacted) {
  bool newReading = gs.readingSeq != s_readingsAtWake;
  char status[32] = "";
  if (!contacted) statusForFailure(status, sizeof(status));
  if (strcmp(status, s_rtc.status) != 0) {
    strlcpy(s_rtc.status, status, sizeof(s_rtc.status));
    strlcpy(s_status, status, sizeof(s_status));
    ui.requestRedraw();
  }
  // radios off before the (slow) panel refresh
  obbStop();
  miBandStop();
  xdrip4iosStop();
  setupServerDropClients();
  wifiSleep();
  // alarms are evaluated once per wake, after the fetch, so a late reading
  // does not trigger a spurious "no readings" sound
  alarms.evaluateNow();
  ui.flush();
  gs.saveRtc();
  alarms.save();
  int32_t s = computeSleepS(contacted, newReading);
  logAdd("sleep %lds (%s%s)", (long)s, contacted ? "ok" : "fail",
         newReading ? ", new" : "");
  enterDeepSleep(s);
}

void cycleTick() {
  static bool latched = false;
  if (!latched) { latched = true; latchCounters(); }

  // Timed setup window over: stop advertising. A connected client does not
  // extend it by itself - the setup app renews the hold with every Info or
  // Config access, while an idle link (Android's own service lookup after a
  // phone reboot stays connected for a long time) must not keep the device
  // awake and the source paused.
  if (s_awakeUntilMs && (int32_t)(s_awakeUntilMs - millis()) <= 0 && setupServerAdvertising()) {
    setupServerAdvertise(false);
    s_awakeUntilMs = 0;
  }
  if (cycleAwake() && !s_sleepNow) return;

  uint32_t window = cfg.source == SRC_OBB ? WINDOW_BLE_MS :
                    (cfg.source == SRC_MIBAND || cfg.source == SRC_XDRIP4IOS) ? WINDOW_PUSH_MS : WINDOW_WIFI_MS;
  bool contacted = gs.sourceSeq != s_seqAtWake;
  bool newReading = gs.readingSeq != s_readingsAtWake;
  // the push sources resend their latest reading on every connection: a stale
  // one does not end the window, the new one (or the timeout) does
  bool push = cfg.source == SRC_MIBAND || cfg.source == SRC_XDRIP4IOS;
  bool over = s_sleepNow || (contacted && (newReading || !push)) || (millis() - s_wakeMs) > window;
  if (!over) return;
  if (ui.busy() || audio.isPlaying()) return;
  finishAndSleep(contacted);
}
