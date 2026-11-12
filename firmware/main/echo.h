/*
 * Local echo application: waits for speech (VAD), records it and plays it back.
 * Recording and playback alternate, so the speaker cannot cause acoustic feedback.
 */
#pragma once

#include "esp_err.h"

/** Allocate buffers and start the echo task. Audio and LEDs must be initialised. */
esp_err_t echo_start(void);
