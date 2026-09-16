/*
  Audio.cpp - ES8311 codec + I2S tone generator
  (part of WaveshareMon, GPL v3, see LICENSE)

  Codec bring-up and the sine synthesis task are ported from the Waveshare
  Touch-LCD-3.5 HAL of M5_NightscoutMon (same ES8311 codec, MCLK derived from
  BCLK so no MCLK pin is needed).

  Copyright (C) 2026 Patrick Sonnerat
*/
#include "Audio.h"
#include "Board.h"
#include "BoardPower.h"
#include <Wire.h>
#include <driver/i2s.h>
#include <math.h>

Audio audio;

#define I2S_PORT   I2S_NUM_0
#define I2S_RATE   16000
#define TONE_CHUNK 128

static volatile bool     s_playing = false;
static volatile uint32_t s_freq = 440;
static volatile uint32_t s_end = 0;
static volatile uint8_t  s_vol = 128;      // 0..255 sample scale

static bool i2cWrite8(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}
static bool i2cRead8(uint8_t addr, uint8_t reg, uint8_t &val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, (uint8_t)1) != 1) return false;
  val = Wire.read();
  return true;
}

static bool es8311Init() {
  const uint8_t A = I2C_ADDR_ES8311;
  uint8_t v;
  if (!i2cRead8(A, 0xFD, v)) return false;          // chip ID1 - presence check
  i2cWrite8(A, 0x00, 0x1F); delay(20);
  i2cWrite8(A, 0x00, 0x00);
  i2cWrite8(A, 0x00, 0x80);
  i2cWrite8(A, 0x01, 0xBF);                          // clocks on, MCLK from BCLK
  if (i2cRead8(A, 0x02, v)) i2cWrite8(A, 0x02, (v & 0x07) | (2 << 3));
  i2cWrite8(A, 0x03, 0x10);
  i2cWrite8(A, 0x04, 0x10);
  i2cWrite8(A, 0x05, 0x00);
  if (i2cRead8(A, 0x06, v)) i2cWrite8(A, 0x06, (v & 0xC0) | 3);   // bclk_div 4
  if (i2cRead8(A, 0x07, v)) i2cWrite8(A, 0x07, (v & 0xC0));
  i2cWrite8(A, 0x08, 0xFF);
  if (i2cRead8(A, 0x00, v)) i2cWrite8(A, 0x00, v & 0xBF);         // slave mode
  i2cWrite8(A, 0x09, 4 << 2);                        // I2S, 32-bit
  i2cWrite8(A, 0x0A, 4 << 2);
  i2cWrite8(A, 0x0D, 0x01);
  i2cWrite8(A, 0x0E, 0x02);
  i2cWrite8(A, 0x12, 0x00);
  i2cWrite8(A, 0x13, 0x10);
  i2cWrite8(A, 0x1C, 0x6A);
  i2cWrite8(A, 0x37, 0x08);
  i2cWrite8(A, 0x32, 0xCC);                          // DAC volume ~80 %
  if (i2cRead8(A, 0x31, v)) i2cWrite8(A, 0x31, v & ~0x60);        // unmute
  return true;
}

// amplifier gate: a control line on the S3 boards; on the C6 the amplifier
// has none, so the codec's DAC mute does the job
static void ampEnable(bool on) {
#if PIN_PA_CTRL >= 0
  pinMode(PIN_PA_CTRL, OUTPUT);
  digitalWrite(PIN_PA_CTRL, on ? HIGH : LOW);
#else
  uint8_t v;
  if (i2cRead8(I2C_ADDR_ES8311, 0x31, v)) i2cWrite8(I2C_ADDR_ES8311, 0x31, on ? (v & ~0x60) : (v | 0x60));
#endif
}

static void toneTask(void *) {
  float phase = 0.0f;
  static int32_t buf[TONE_CHUNK * 2];
  for (;;) {
    if (s_playing) {
      if ((int32_t)(millis() - s_end) >= 0) {
        s_playing = false;
        i2s_zero_dma_buffer(I2S_PORT);
        ampEnable(false);
        continue;
      }
      float step = 2.0f * PI * (float)s_freq / (float)I2S_RATE;
      int32_t amp = (int32_t)s_vol * 128;
      for (int i = 0; i < TONE_CHUNK; ++i) {
        int32_t s = (int32_t)(sinf(phase) * amp) << 16;
        buf[2 * i] = s;
        buf[2 * i + 1] = s;
        phase += step;
        if (phase > 2.0f * PI) phase -= 2.0f * PI;
      }
      size_t written = 0;
      i2s_write(I2S_PORT, buf, sizeof(buf), &written, portMAX_DELAY);
    } else {
      phase = 0.0f;
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
}

bool Audio::begin() {
  if (tried) return enabled;
  tried = true;
  boardAudioPower(true);               // audio rail on (it already is, see PowerCycle)
  ampEnable(false);                    // amplifier idle until a tone plays
  delay(10);
  if (!es8311Init()) {
    Serial.println("[audio] ES8311 not found, speaker disabled");
    return false;
  }
  i2s_config_t c = {};
  c.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  c.sample_rate = I2S_RATE;
  c.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  c.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  c.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  c.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  c.dma_buf_count = 4;
  c.dma_buf_len = 256;
  c.tx_desc_auto_clear = true;
  if (i2s_driver_install(I2S_PORT, &c, 0, nullptr) != ESP_OK) {
    Serial.println("[audio] I2S install failed");
    return false;
  }
  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = PIN_I2S_BCK;
  pins.ws_io_num = PIN_I2S_LRCK;
  pins.data_out_num = PIN_I2S_DOUT;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  i2s_set_pin(I2S_PORT, &pins);
  i2s_zero_dma_buffer(I2S_PORT);
  enabled = true;
  xTaskCreatePinnedToCore(toneTask, "tone", 3072, nullptr, 2, nullptr, 1);
  return true;
}

void Audio::tone(uint16_t freq, uint32_t durationMs, uint8_t volume) {
  if (freq == 0 || durationMs == 0) return;
  if (!enabled && !begin()) return;      // codec brought up on first use only
  s_vol = (uint8_t)map(volume > 100 ? 100 : volume, 0, 100, 0, 255);
  s_freq = freq;
  s_end = millis() + durationMs;
  ampEnable(true);
  s_playing = true;
}

bool Audio::isPlaying() const { return s_playing; }

void Audio::mute() {
  s_playing = false;
  if (enabled) i2s_zero_dma_buffer(I2S_PORT);
  ampEnable(false);
}

void Audio::powerDown() {
  mute();                              // amplifier idle; the codec rail stays on (PowerCycle.cpp)
}
