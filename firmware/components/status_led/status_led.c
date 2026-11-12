#include "status_led.h"

#include <stdatomic.h>
#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "status_led";

#define BLINK_HALF_PERIOD_US (150 * 1000)

typedef enum { LED_OFF, LED_ON, LED_BLINK } led_mode_t;

enum { GREEN, YELLOW, RED, LED_COUNT };

/* Status -> mode of each LED. Order matches status_t. */
static const led_mode_t PATTERNS[STATUS_COUNT_][LED_COUNT] = {
    [STATUS_OFF] = {LED_OFF, LED_OFF, LED_OFF},
    [STATUS_BOOTING] = {LED_OFF, LED_ON, LED_OFF},
    [STATUS_LISTENING] = {LED_ON, LED_OFF, LED_OFF},
    [STATUS_RECORDING] = {LED_BLINK, LED_OFF, LED_OFF},
    [STATUS_BUSY] = {LED_OFF, LED_ON, LED_OFF},
    [STATUS_PLAYING] = {LED_OFF, LED_BLINK, LED_OFF},
    [STATUS_OFFLINE] = {LED_OFF, LED_OFF, LED_BLINK},
    [STATUS_ERROR] = {LED_OFF, LED_OFF, LED_ON},
};

static int s_gpio[LED_COUNT];
static atomic_int s_status = STATUS_OFF;
static atomic_bool s_phase;
static esp_timer_handle_t s_timer;

static void refresh(void)
{
    const int status = atomic_load(&s_status);
    const bool phase = atomic_load(&s_phase);
    for (int i = 0; i < LED_COUNT; i++) {
        const led_mode_t mode = PATTERNS[status][i];
        const bool on = mode == LED_ON || (mode == LED_BLINK && phase);
        gpio_set_level(s_gpio[i], on ? 1 : 0);
    }
}

static void on_timer(void *arg)
{
    (void)arg;
    atomic_store(&s_phase, !atomic_load(&s_phase));
    refresh();
}

esp_err_t status_led_init(const status_led_config_t *cfg)
{
    ESP_RETURN_ON_FALSE(s_timer == NULL, ESP_ERR_INVALID_STATE, TAG, "already initialised");

    s_gpio[GREEN] = cfg->green_gpio;
    s_gpio[YELLOW] = cfg->yellow_gpio;
    s_gpio[RED] = cfg->red_gpio;

    const gpio_config_t io = {
        .pin_bit_mask =
            (1ULL << cfg->green_gpio) | (1ULL << cfg->yellow_gpio) | (1ULL << cfg->red_gpio),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "gpio_config");
    refresh();

    const esp_timer_create_args_t timer_args = {
        .callback = on_timer,
        .name = "status_led",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &s_timer), TAG, "timer");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(s_timer, BLINK_HALF_PERIOD_US), TAG,
                        "timer start");

    ESP_LOGI(TAG, "LEDs: green %d, yellow %d, red %d", cfg->green_gpio, cfg->yellow_gpio,
             cfg->red_gpio);
    return ESP_OK;
}

void status_led_set(status_t status)
{
    if ((int)status < 0 || status >= STATUS_COUNT_) {
        status = STATUS_ERROR;
    }
    atomic_store(&s_status, (int)status);
    refresh(); /* show the change now, not at the next timer tick */
}
