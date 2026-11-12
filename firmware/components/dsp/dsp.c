#include "dsp.h"

#include <math.h>

/* Pole of the DC blocker; 0.995 puts the -3 dB point at about 13 Hz for 16 kHz. */
#define DC_BLOCKER_POLE 0.995f

void dsp_dc_blocker_reset(dsp_dc_blocker_t *state)
{
    state->prev_in = 0.0f;
    state->prev_out = 0.0f;
}

static int16_t saturate_i16(float value)
{
    if (value > (float)INT16_MAX) {
        return INT16_MAX;
    }
    if (value < (float)INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)value;
}

void dsp_mic_to_pcm16(const int32_t *in, int16_t *out, size_t count, unsigned shift,
                      dsp_dc_blocker_t *dc)
{
    /* Scale with a float multiply rather than >> to avoid implementation-defined
     * behaviour of right-shifting negative integers. */
    const float scale = ldexpf(1.0f, -(int)shift);

    for (size_t i = 0; i < count; i++) {
        const float x = (float)in[i] * scale;
        const float y = x - dc->prev_in + DC_BLOCKER_POLE * dc->prev_out;
        dc->prev_in = x;
        dc->prev_out = y;
        out[i] = saturate_i16(y);
    }
}

float dsp_rms(const int16_t *samples, size_t count)
{
    if (count == 0) {
        return 0.0f;
    }
    float sum = 0.0f;
    for (size_t i = 0; i < count; i++) {
        const float s = (float)samples[i];
        sum += s * s;
    }
    return sqrtf(sum / (float)count);
}

int32_t dsp_peak(const int16_t *samples, size_t count)
{
    int32_t peak = 0;
    for (size_t i = 0; i < count; i++) {
        int32_t v = samples[i];
        if (v < 0) {
            v = -v;
        }
        if (v > peak) {
            peak = v;
        }
    }
    return peak;
}
