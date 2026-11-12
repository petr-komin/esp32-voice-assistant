/*
 * Audio I/O: I2S MEMS microphone (capture) and I2S class-D amplifier (playback).
 *
 * Both directions use 16 kHz, 16-bit, mono PCM. The microphone and the
 * amplifier each get their own I2S controller so they can run independently.
 * The module is not thread-safe: capture and playback functions must each be
 * called from a single task.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_SAMPLE_RATE_HZ 16000

typedef struct {
    int bclk_gpio;
    int ws_gpio;
    int data_gpio;       /**< microphone SD output -> ESP32 input */
    bool right_slot;     /**< INMP441 L/R pin high selects the right slot */
    unsigned gain_shift; /**< see dsp_mic_to_pcm16(); 16 = unity, 14 = +12 dB */
} audio_mic_config_t;

typedef struct {
    int bclk_gpio;
    int ws_gpio;
    int data_gpio; /**< ESP32 output -> amplifier DIN */
} audio_speaker_config_t;

esp_err_t audio_mic_init(const audio_mic_config_t *cfg);

/**
 * Read exactly @p samples samples of 16-bit PCM, blocking until they arrive.
 *
 * @return ESP_OK, or ESP_ERR_TIMEOUT if the I2S driver did not deliver data
 *         within @p timeout_ms for any chunk.
 */
esp_err_t audio_mic_read(int16_t *out, size_t samples, uint32_t timeout_ms);

/** Drop audio buffered in DMA while nobody was reading (e.g. during playback). */
void audio_mic_discard(void);

esp_err_t audio_speaker_init(const audio_speaker_config_t *cfg);

/** Queue PCM for playback, blocking while the DMA buffers are full. */
esp_err_t audio_speaker_write(const int16_t *pcm, size_t samples, uint32_t timeout_ms);

/** Time the DMA buffers need to play out what was last queued, in milliseconds. */
uint32_t audio_speaker_drain_ms(void);

#ifdef __cplusplus
}
#endif
