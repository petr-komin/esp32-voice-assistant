/*
 * Energy-based voice activity detection (VAD).
 *
 * The detector is fed the RMS level of consecutive fixed-size blocks
 * (20 ms in this project). It tracks the background noise floor and reports
 * speech when the level stays a configurable factor above it.
 *
 * Pure C with no ESP-IDF dependency, unit-tested on the host.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float start_factor;    /**< speech starts above noise * start_factor */
    float stop_factor;     /**< silence is below noise * stop_factor */
    float min_level;       /**< never start below this RMS level (int16 units) */
    uint16_t start_blocks; /**< consecutive loud blocks needed to start */
    uint16_t hang_blocks;  /**< consecutive quiet blocks needed to end */
} vad_config_t;

/** Defaults tuned on an INMP441 at ~30 cm with 20 ms blocks. */
#define VAD_CONFIG_DEFAULT()                                                                       \
    {                                                                                              \
        .start_factor = 3.0f,                                                                      \
        .stop_factor = 2.0f,                                                                       \
        .min_level = 500.0f,                                                                       \
        .start_blocks = 3,                                                                         \
        .hang_blocks = 40,                                                                         \
    }

typedef enum {
    VAD_SILENCE, /**< no speech */
    VAD_START,   /**< speech has just started (emitted once) */
    VAD_SPEECH,  /**< speech continues */
    VAD_END,     /**< speech has just ended (emitted once) */
} vad_event_t;

typedef struct {
    vad_config_t cfg;
    float noise; /**< current noise-floor estimate; 0 until the first block */
    float level; /**< level of the last processed block */
    bool speaking;
    uint16_t loud;  /**< consecutive blocks above the start threshold */
    uint16_t quiet; /**< consecutive blocks below the stop threshold */
} vad_t;

/** Initialise the detector. The noise floor is learned from the first blocks. */
void vad_init(vad_t *vad, const vad_config_t *cfg);

/** Abort any speech in progress; keeps the learned noise floor. */
void vad_reset(vad_t *vad);

/** Feed the RMS level of the next block and get the resulting event. */
vad_event_t vad_process(vad_t *vad, float block_level);

float vad_start_threshold(const vad_t *vad);
float vad_stop_threshold(const vad_t *vad);

/*
 * Pre-roll buffer: keeps the last N blocks before speech is detected so the
 * beginning of the first word is not cut off. Storage is supplied by the
 * caller (no dynamic allocation).
 */
typedef struct {
    int16_t *storage; /**< block_samples * capacity_blocks samples */
    size_t block_samples;
    size_t capacity_blocks;
    size_t head;   /**< next block slot to write */
    size_t filled; /**< number of valid blocks */
} vad_preroll_t;

void vad_preroll_init(vad_preroll_t *pr, int16_t *storage, size_t block_samples,
                      size_t capacity_blocks);

/** Store one block (block_samples samples), overwriting the oldest when full. */
void vad_preroll_push(vad_preroll_t *pr, const int16_t *block);

/**
 * Copy the stored blocks, oldest first, into @p out and empty the buffer.
 * At most @p out_capacity samples are written; the newest blocks are kept
 * when the output is too small.
 *
 * @return number of samples written
 */
size_t vad_preroll_drain(vad_preroll_t *pr, int16_t *out, size_t out_capacity);

#ifdef __cplusplus
}
#endif
