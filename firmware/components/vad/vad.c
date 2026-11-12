#include "vad.h"

#include <string.h>

/* The noise floor follows falling levels quickly and rising levels slowly
 * (time constant ~2 s at 20 ms blocks), so short noises do not raise it. */
#define NOISE_FALL_RATE 0.1f
#define NOISE_RISE_RATE 0.01f

/* The stop threshold never drops below this fraction of min_level. */
#define STOP_MIN_RATIO 0.6f

static float max_f(float a, float b)
{
    return a > b ? a : b;
}

void vad_init(vad_t *vad, const vad_config_t *cfg)
{
    memset(vad, 0, sizeof(*vad));
    vad->cfg = *cfg;
}

void vad_reset(vad_t *vad)
{
    vad->speaking = false;
    vad->loud = 0;
    vad->quiet = 0;
}

float vad_start_threshold(const vad_t *vad)
{
    return max_f(vad->noise * vad->cfg.start_factor, vad->cfg.min_level);
}

float vad_stop_threshold(const vad_t *vad)
{
    return max_f(vad->noise * vad->cfg.stop_factor, vad->cfg.min_level * STOP_MIN_RATIO);
}

vad_event_t vad_process(vad_t *vad, float block_level)
{
    vad->level = block_level;
    if (vad->noise == 0.0f) {
        vad->noise = block_level;
    }

    if (!vad->speaking) {
        if (block_level > vad_start_threshold(vad)) {
            vad->loud++;
            if (vad->loud >= vad->cfg.start_blocks) {
                vad->speaking = true;
                vad->quiet = 0;
                return VAD_START;
            }
        } else {
            vad->loud = 0;
            const float rate = block_level < vad->noise ? NOISE_FALL_RATE : NOISE_RISE_RATE;
            vad->noise += rate * (block_level - vad->noise);
        }
        return VAD_SILENCE;
    }

    if (block_level > vad_stop_threshold(vad)) {
        vad->quiet = 0;
        return VAD_SPEECH;
    }
    vad->quiet++;
    if (vad->quiet >= vad->cfg.hang_blocks) {
        vad->speaking = false;
        vad->loud = 0;
        return VAD_END;
    }
    return VAD_SPEECH;
}

void vad_preroll_init(vad_preroll_t *pr, int16_t *storage, size_t block_samples,
                      size_t capacity_blocks)
{
    pr->storage = storage;
    pr->block_samples = block_samples;
    pr->capacity_blocks = capacity_blocks;
    pr->head = 0;
    pr->filled = 0;
}

void vad_preroll_push(vad_preroll_t *pr, const int16_t *block)
{
    if (pr->capacity_blocks == 0) {
        return;
    }
    memcpy(&pr->storage[pr->head * pr->block_samples], block, pr->block_samples * sizeof(int16_t));
    pr->head = (pr->head + 1) % pr->capacity_blocks;
    if (pr->filled < pr->capacity_blocks) {
        pr->filled++;
    }
}

size_t vad_preroll_drain(vad_preroll_t *pr, int16_t *out, size_t out_capacity)
{
    if (pr->filled == 0 || pr->block_samples == 0) {
        pr->filled = 0;
        return 0;
    }
    size_t blocks = pr->filled;
    const size_t fit = out_capacity / pr->block_samples;
    if (blocks > fit) {
        blocks = fit;
    }

    /* Index of the oldest block that is still copied. */
    size_t index = (pr->head + pr->capacity_blocks - blocks) % pr->capacity_blocks;
    for (size_t i = 0; i < blocks; i++) {
        memcpy(&out[i * pr->block_samples], &pr->storage[index * pr->block_samples],
               pr->block_samples * sizeof(int16_t));
        index = (index + 1) % pr->capacity_blocks;
    }

    pr->filled = 0;
    return blocks * pr->block_samples;
}
