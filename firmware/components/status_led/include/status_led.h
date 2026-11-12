/*
 * Device status shown on three discrete LEDs (green, yellow, red).
 *
 *   STATUS_BOOTING    yellow on        start-up, calibrating
 *   STATUS_LISTENING  green on         ready, waiting for speech
 *   STATUS_RECORDING  green blinking   capturing speech
 *   STATUS_BUSY       yellow on        processing / waiting for the server
 *   STATUS_PLAYING    yellow blinking  playing a reply
 *   STATUS_OFFLINE    red blinking     no network or server
 *   STATUS_ERROR      red on           fault
 *
 * Blinking is driven by an esp_timer, so it keeps running while the
 * application task blocks on audio I/O.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    STATUS_OFF,
    STATUS_BOOTING,
    STATUS_LISTENING,
    STATUS_RECORDING,
    STATUS_BUSY,
    STATUS_PLAYING,
    STATUS_OFFLINE,
    STATUS_ERROR,
    STATUS_COUNT_,
} status_t;

typedef struct {
    int green_gpio;
    int yellow_gpio;
    int red_gpio;
} status_led_config_t;

esp_err_t status_led_init(const status_led_config_t *cfg);

/** Show a status; safe to call from any task. */
void status_led_set(status_t status);

#ifdef __cplusplus
}
#endif
