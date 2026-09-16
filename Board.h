/*
  Board.h - the three Waveshare 1.54" e-paper boards, chosen at compile time
  (part of WaveshareMon, GPL v3, see LICENSE)

  One code base, one firmware image per board (Scripts/build.ps1 -Target):
    WSMON_BOARD_S3_4C  ESP32-S3-ePaper-1.54G  four-colour panel (default)
    WSMON_BOARD_S3_BW  ESP32-S3-ePaper-1.54   black-and-white panel, same board
    WSMON_BOARD_C6_BW  ESP32-C6-ePaper-1.54   black-and-white panel, ESP32-C6,
                       power switches on a TCA9554 I/O expander
  Pin maps from Waveshare's own examples (github.com/waveshareteam). All
  three panels are 200 x 200; only the colour depth and the refresh time
  differ (EpdUi.cpp). BoardPower.h hides the power-switch differences.

  Copyright (C) 2026 Patrick Sonnerat
*/
#ifndef BOARD_H
#define BOARD_H

#if !defined(WSMON_BOARD_S3_4C) && !defined(WSMON_BOARD_S3_BW) && !defined(WSMON_BOARD_C6_BW)
#define WSMON_BOARD_S3_4C 1
#endif

// I2C device addresses, common to the three boards
#define I2C_ADDR_RTC     0x51
#define I2C_ADDR_SHTC3   0x70
#define I2C_ADDR_ES8311  0x18
#define I2C_ADDR_TCA9554 0x20

#if defined(WSMON_BOARD_C6_BW)
// ---------------------------------------------------------------- ESP32-C6, B/W
#define BOARD_NAME        "ESP32-C6-ePaper-1.54"
#define BOARD_FOLDER      "WS_ePaperC6_154"   // Binaries/<folder>: flasher + update images
#define BOARD_PANEL_COLOR 0                   // 1 = four-colour panel, 0 = black and white

// 1.54" B/W e-paper (SSD1681 class), SPI2; the TF card shares the bus (not used)
#define PIN_EPD_SCK    6
#define PIN_EPD_MOSI   5
#define PIN_EPD_CS     7
#define PIN_EPD_DC     15
#define PIN_EPD_RST    11
#define PIN_EPD_BUSY   10
#define PIN_EPD_PWR    -1    // panel supply: expander bit EXIO_EPD_PWR

// I2C bus: PCF85063 RTC, SHTC3, ES8311 codec, TCA9554 expander, FT6336 touch
#define PIN_I2C_SDA    18
#define PIN_I2C_SCL    8

// audio: ES8311 over I2S; the amplifier has no control line of its own here,
// the whole audio rail is an expander output and the codec's mute gates the sound
#define PIN_I2S_MCK    19
#define PIN_I2S_BCK    21
#define PIN_I2S_LRCK   22
#define PIN_I2S_DOUT   23
#define PIN_I2S_DIN    20
#define PIN_PA_CTRL    -1
#define PIN_PA_EN      -1    // audio rail: expander bit EXIO_AUDIO_PWR

// power / battery
#define PIN_BAT_ADC    0     // VBAT / 2 (ADC1 channel 0)
#define PIN_VBAT_PWR   -1    // battery latch: expander bit EXIO_VBAT_PWR
#define PIN_PWR_BTN    2     // LP GPIO: wakes from deep sleep
#define PIN_BOOT_BTN   9     // not an LP GPIO on the C6: cannot wake from deep sleep
#define BOARD_BOOT_WAKES 0

// TCA9554 expander outputs (HIGH = on)
#define BOARD_EXPANDER 1
#define EXIO_EPD_PWR   0
#define EXIO_AUDIO_PWR 1
#define EXIO_LED       4
#define EXIO_VBAT_PWR  5

#else
// ------------------------------------------------------ ESP32-S3, 4-colour or B/W
#if defined(WSMON_BOARD_S3_BW)
#define BOARD_NAME        "ESP32-S3-ePaper-1.54"
#define BOARD_FOLDER      "WS_ePaper154"
#define BOARD_PANEL_COLOR 0
#else
#define BOARD_NAME        "ESP32-S3-ePaper-1.54G"
#define BOARD_FOLDER      "WS_ePaper154G"
#define BOARD_PANEL_COLOR 1
#endif

// 1.54" e-paper, SPI
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
#define BOARD_BOOT_WAKES 1
#define BOARD_EXPANDER 0
#endif

#endif
