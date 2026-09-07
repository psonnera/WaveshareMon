/*
  Board.h - Waveshare ESP32-S3-ePaper-1.54G pin map
  (part of WaveshareMon, GPL v3, see LICENSE)

  Pin assignments verified against the Waveshare example package
  (github.com/waveshareteam/ESP32-S3-ePaper-1.54G, Arduino_3.2.0 examples).

  Copyright (C) 2026 Patrick Sonnerat
*/
#ifndef BOARD_H
#define BOARD_H

// 1.54" 4-colour e-paper (G), SPI
#define PIN_EPD_SCK    12
#define PIN_EPD_MOSI   13
#define PIN_EPD_CS     11
#define PIN_EPD_DC     10
#define PIN_EPD_RST    9
#define PIN_EPD_BUSY   8
#define PIN_EPD_PWR    6     // panel supply switch, LOW = on

// I2C bus: PCF85063 RTC (0x51), SHTC3 (0x70), ES8311 codec (0x18)
#define PIN_I2C_SDA    47
#define PIN_I2C_SCL    48
#define I2C_ADDR_RTC   0x51
#define I2C_ADDR_ES8311 0x18

// audio: ES8311 over I2S + power amplifier
#define PIN_I2S_MCK    14
#define PIN_I2S_BCK    15
#define PIN_I2S_LRCK   38
#define PIN_I2S_DOUT   45
#define PIN_I2S_DIN    16
#define PIN_PA_CTRL    46    // HIGH = amplifier active
#define PIN_PA_EN      42    // LOW = audio power on

// power / battery
#define PIN_BAT_ADC    4     // VBAT / 2
#define PIN_VBAT_PWR   17    // HIGH = keep the board powered from the battery
#define PIN_PWR_BTN    18
#define PIN_BOOT_BTN   0     // BOOT, usable as an input after boot (LOW = pressed)

#endif
