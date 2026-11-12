/*
 * Small signal-processing helpers for 16 kHz mono speech.
 *
 * Pure C with no ESP-IDF dependency, so it is unit-tested on the host
 * (see test/host).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** State of a one-pole DC-blocking high-pass filter (cut-off ~13 Hz at 16 kHz). */
typedef struct {
    float prev_in;
    float prev_out;
} dsp_dc_blocker_t;

/** Reset the filter to its initial (silent) state. */
void dsp_dc_blocker_reset(dsp_dc_blocker_t *state);

/**
 * Convert raw I2S microphone words to 16-bit PCM.
 *
 * MEMS microphones such as the INMP441 deliver 24-bit samples left-aligned in
 * a 32-bit slot. Each sample is scaled by 2^-shift, passed through the DC
 * blocker and saturated to the int16 range. shift = 16 yields the top 16 bits;
 * every step below 16 adds 6 dB of gain.
 *
 * @param in     raw 32-bit words from the I2S peripheral
 * @param out    destination; may alias @p in (conversion runs front to back)
 * @param count  number of samples
 * @param shift  gain shift, 0..31
 * @param dc     filter state carried across calls
 */
void dsp_mic_to_pcm16(const int32_t *in, int16_t *out, size_t count, unsigned shift,
                      dsp_dc_blocker_t *dc);

/** Root-mean-square level of a block, in int16 units (0..32768). Returns 0 for count == 0. */
float dsp_rms(const int16_t *samples, size_t count);

/** Largest absolute sample value in a block. */
int32_t dsp_peak(const int16_t *samples, size_t count);

#ifdef __cplusplus
}
#endif
