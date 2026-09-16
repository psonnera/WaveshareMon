/*
  BoardPower.cpp - power switches, battery latch and deep-sleep wake pins per board
  (part of WaveshareMon, GPL v3, see LICENSE)

  Copyright (C) 2026 Patrick Sonnerat
*/
#include "BoardPower.h"
#include "Board.h"
#include <Wire.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>

#if BOARD_EXPANDER
// ---- TCA9554: 0x00 input, 0x01 output, 0x02 polarity, 0x03 configuration (1 = input)
static uint8_t s_exioOut = 0;             // shadow of the output register

static void exioWrite(uint8_t reg, uint8_t v) {
  Wire.beginTransmission(I2C_ADDR_TCA9554);
  Wire.write(reg);
  Wire.write(v);
  Wire.endTransmission();
}

static void exioSet(uint8_t bit, bool on) {
  if (on) s_exioOut |= (1 << bit); else s_exioOut &= ~(1 << bit);
  exioWrite(0x01, s_exioOut);
}

static void exioBegin() {
  // read back the outputs so a wake from sleep keeps what was set before
  Wire.beginTransmission(I2C_ADDR_TCA9554);
  Wire.write(0x01);
  if (Wire.endTransmission(false) == 0 && Wire.requestFrom((int)I2C_ADDR_TCA9554, 1) == 1) s_exioOut = Wire.read();
  // the three switches and the LED are outputs, everything else stays an input
  uint8_t outputs = (1 << EXIO_EPD_PWR) | (1 << EXIO_AUDIO_PWR) | (1 << EXIO_VBAT_PWR) | (1 << EXIO_LED);
  s_exioOut |= (1 << EXIO_EPD_PWR) | (1 << EXIO_AUDIO_PWR) | (1 << EXIO_VBAT_PWR);   // rails + latch on
  s_exioOut &= ~(1 << EXIO_LED);
  exioWrite(0x01, s_exioOut);
  exioWrite(0x03, (uint8_t)~outputs);
}
#endif

void boardPowerBegin() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
#if BOARD_EXPANDER
  // the battery latch is an expander output: drive it before anything else,
  // the board runs on the PWR button until then
  exioBegin();
#else
  // The battery latch must stay closed through deep sleep (held) and be driven
  // again before the hold is released, or the board would lose power here.
  pinMode(PIN_VBAT_PWR, OUTPUT);
  digitalWrite(PIN_VBAT_PWR, HIGH);
  // Both switched rails stay on at all times: the PCF85063 RTC is only
  // readable while the audio rail (ES8311 codec, PA_EN) is powered - an
  // unpowered codec loads the shared I2C bus - and the hibernated panel
  // draws next to nothing. Only the amplifier itself (PA_CTRL) is gated.
  pinMode(PIN_EPD_PWR, OUTPUT);
  digitalWrite(PIN_EPD_PWR, LOW);
  pinMode(PIN_PA_EN, OUTPUT);
  digitalWrite(PIN_PA_EN, LOW);
  gpio_hold_dis((gpio_num_t)PIN_VBAT_PWR);
  gpio_hold_dis((gpio_num_t)PIN_EPD_PWR);
  gpio_hold_dis((gpio_num_t)PIN_PA_EN);
  gpio_deep_sleep_hold_dis();
#endif
  // the buttons were RTC wake pads: hand them back to the digital GPIO matrix
  rtc_gpio_deinit((gpio_num_t)PIN_PWR_BTN);
#if BOARD_BOOT_WAKES
  rtc_gpio_deinit((gpio_num_t)PIN_BOOT_BTN);
#endif
}

void boardPanelPower(bool on) {
#if BOARD_EXPANDER
  exioSet(EXIO_EPD_PWR, on);
#else
  pinMode(PIN_EPD_PWR, OUTPUT);
  digitalWrite(PIN_EPD_PWR, on ? LOW : HIGH);
#endif
}

void boardAudioPower(bool on) {
#if BOARD_EXPANDER
  exioSet(EXIO_AUDIO_PWR, on);
#else
  pinMode(PIN_PA_EN, OUTPUT);
  digitalWrite(PIN_PA_EN, on ? LOW : HIGH);
#endif
}

void boardBatteryLatch(bool on) {
#if BOARD_EXPANDER
  exioSet(EXIO_VBAT_PWR, on);
#else
  pinMode(PIN_VBAT_PWR, OUTPUT);
  digitalWrite(PIN_VBAT_PWR, on ? HIGH : LOW);
#endif
}

void boardLed(bool on) {
#if BOARD_EXPANDER
  exioSet(EXIO_LED, on);
#else
  (void)on;
#endif
}

void boardPrepareSleep(bool pwrOnly) {
#if !BOARD_EXPANDER
  // rails and battery latch kept and held through sleep
  gpio_hold_en((gpio_num_t)PIN_EPD_PWR);
  gpio_hold_en((gpio_num_t)PIN_PA_EN);
  gpio_hold_en((gpio_num_t)PIN_VBAT_PWR);
  gpio_deep_sleep_hold_en();
#endif
  // the buttons (active low) wake the chip
  uint64_t mask = 1ULL << PIN_PWR_BTN;
  rtc_gpio_pullup_en((gpio_num_t)PIN_PWR_BTN);
  rtc_gpio_pulldown_dis((gpio_num_t)PIN_PWR_BTN);
#if BOARD_BOOT_WAKES
  if (!pwrOnly) {
    rtc_gpio_pullup_en((gpio_num_t)PIN_BOOT_BTN);
    rtc_gpio_pulldown_dis((gpio_num_t)PIN_BOOT_BTN);
    mask |= 1ULL << PIN_BOOT_BTN;
  }
#else
  (void)pwrOnly;
#endif
  esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);
}

void boardPowerOff() {
#if BOARD_EXPANDER
  exioSet(EXIO_VBAT_PWR, false);           // on battery the board is gone right here
#else
  gpio_deep_sleep_hold_dis();              // the latch pin is not held: on battery the board powers down
#endif
  rtc_gpio_pullup_en((gpio_num_t)PIN_PWR_BTN);
  rtc_gpio_pulldown_dis((gpio_num_t)PIN_PWR_BTN);
  esp_sleep_enable_ext1_wakeup(1ULL << PIN_PWR_BTN, ESP_EXT1_WAKEUP_ANY_LOW);
}

bool boardWakeWasPwr() {
  uint64_t pins = esp_sleep_get_ext1_wakeup_status();
  return (pins & (1ULL << PIN_PWR_BTN)) != 0 || !BOARD_BOOT_WAKES;
}
