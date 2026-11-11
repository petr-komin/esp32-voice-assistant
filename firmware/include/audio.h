#pragma once

// Společné funkce pro mikrofon a reproduktor.
// Audio je všude 16 kHz, 16 bit, mono.

#include <Arduino.h>
#include <driver/i2s.h>
#include "piny.h"

const int SAMPLE_RATE = 16000;

const i2s_port_t SPK_PORT = I2S_NUM_0;
const i2s_port_t MIC_PORT = I2S_NUM_1;

// INMP441 posílá 24 bitů zarovnaných doleva ve 32bitovém slotu.
// Posun o 16 by dal přesně horních 16 bitů, posun o 14 přidá zesílení 4×.
// Když je nahrávka moc tichá, zmenši číslo; když chrčí, zvětši.
const int MIC_SHIFT = 14;

// Když je nahrávka úplné ticho (úroveň 0), zkus I2S_CHANNEL_FMT_ONLY_RIGHT.
// Na klasickém ESP32 bývají kanály v ovladači prohozené.
const i2s_channel_fmt_t MIC_CHANNEL = I2S_CHANNEL_FMT_ONLY_LEFT;

inline void speakerInit() {
  i2s_config_t config = {};
  config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  config.sample_rate = SAMPLE_RATE;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  config.dma_buf_count = 8;
  config.dma_buf_len = 256;
  config.tx_desc_auto_clear = true;  // po dohrání posílat ticho

  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = SPK_BCLK;
  pins.ws_io_num = SPK_LRC;
  pins.data_out_num = SPK_DIN;
  pins.data_in_num = I2S_PIN_NO_CHANGE;

  i2s_driver_install(SPK_PORT, &config, 0, nullptr);
  i2s_set_pin(SPK_PORT, &pins);
}

inline void micInit() {
  i2s_config_t config = {};
  config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  config.sample_rate = SAMPLE_RATE;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  config.channel_format = MIC_CHANNEL;
  config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  config.dma_buf_count = 8;
  config.dma_buf_len = 256;

  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = MIC_SCK;
  pins.ws_io_num = MIC_WS;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num = MIC_SD;

  i2s_driver_install(MIC_PORT, &config, 0, nullptr);
  i2s_set_pin(MIC_PORT, &pins);
}

// Zahodí, co se v DMA nahromadilo, zatímco se mikrofon nečetl.
inline void micFlush() {
  int32_t trash[256];
  size_t got;
  do {
    i2s_read(MIC_PORT, trash, sizeof(trash), &got, 0);
  } while (got > 0);
}

// Přečte count vzorků z mikrofonu a převede je na 16 bit.
// Čeká, dokud nejsou všechny (count / 16 000 sekund).
inline void micRead(int16_t* out, size_t count) {
  static int32_t raw[256];
  // Jednopólový filtr odstraní stejnosměrnou složku, kterou INMP441 má.
  static float prevIn = 0, prevOut = 0;

  while (count > 0) {
    size_t n = count < 256 ? count : 256;
    size_t got;
    i2s_read(MIC_PORT, raw, n * sizeof(int32_t), &got, portMAX_DELAY);
    n = got / sizeof(int32_t);

    for (size_t i = 0; i < n; i++) {
      float in = (float)(raw[i] >> MIC_SHIFT);
      float y = in - prevIn + 0.995f * prevOut;
      prevIn = in;
      prevOut = y;
      if (y > 32767) y = 32767;
      if (y < -32768) y = -32768;
      out[i] = (int16_t)y;
    }
    out += n;
    count -= n;
  }
}

// Přehraje mono vzorky. Zesilovač chce stereo, takže se každý vzorek zdvojí.
inline void speakerWrite(const int16_t* mono, size_t count) {
  static int16_t stereo[512];
  while (count > 0) {
    size_t n = count < 256 ? count : 256;
    for (size_t i = 0; i < n; i++) {
      stereo[2 * i] = mono[i];
      stereo[2 * i + 1] = mono[i];
    }
    size_t written;
    i2s_write(SPK_PORT, stereo, n * 2 * sizeof(int16_t), &written, portMAX_DELAY);
    mono += n;
    count -= n;
  }
}

// Špičková a RMS úroveň, ať je v sériové konzoli vidět, že mikrofon něco slyší.
inline void printLevel(const int16_t* samples, size_t count) {
  int peak = 0;
  double sum = 0;
  for (size_t i = 0; i < count; i++) {
    int v = abs(samples[i]);
    if (v > peak) peak = v;
    sum += (double)samples[i] * samples[i];
  }
  Serial.printf("Úroveň: špička %d, RMS %.0f (z 32767)\n", peak, sqrt(sum / count));
}
