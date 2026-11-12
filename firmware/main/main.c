/*
 * Voice terminal firmware: entry point.
 *
 * Brings up the hardware and starts the application task. app_main() returns
 * once the task is running; all work happens in tasks with explicit stacks.
 */
#include "audio.h"
#include "echo.h"
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "sdkconfig.h"
#include "status_led.h"

static const char *TAG = "main";

static esp_err_t init_hardware(void)
{
    const status_led_config_t led_cfg = {
        .green_gpio = CONFIG_VT_LED_GREEN_GPIO,
        .yellow_gpio = CONFIG_VT_LED_YELLOW_GPIO,
        .red_gpio = CONFIG_VT_LED_RED_GPIO,
    };
    ESP_RETURN_ON_ERROR(status_led_init(&led_cfg), TAG, "LEDs");
    status_led_set(STATUS_BOOTING);

    const audio_speaker_config_t speaker_cfg = {
        .bclk_gpio = CONFIG_VT_SPEAKER_BCLK_GPIO,
        .ws_gpio = CONFIG_VT_SPEAKER_WS_GPIO,
        .data_gpio = CONFIG_VT_SPEAKER_DATA_GPIO,
    };
    ESP_RETURN_ON_ERROR(audio_speaker_init(&speaker_cfg), TAG, "speaker");

    const audio_mic_config_t mic_cfg = {
        .bclk_gpio = CONFIG_VT_MIC_BCLK_GPIO,
        .ws_gpio = CONFIG_VT_MIC_WS_GPIO,
        .data_gpio = CONFIG_VT_MIC_DATA_GPIO,
#ifdef CONFIG_VT_MIC_RIGHT_SLOT
        .right_slot = true,
#else
        .right_slot = false,
#endif
        .gain_shift = CONFIG_VT_MIC_GAIN_SHIFT,
    };
    ESP_RETURN_ON_ERROR(audio_mic_init(&mic_cfg), TAG, "microphone");
    return ESP_OK;
}

void app_main(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    ESP_LOGI(TAG, "%s %s (ESP-IDF %s), reset reason %d", app->project_name, app->version,
             app->idf_ver, (int)esp_reset_reason());

    esp_err_t err = init_hardware();
    if (err == ESP_OK) {
        err = echo_start();
    }
    if (err != ESP_OK) {
        /* Leave the red LED on; the watchdog does not help with a wiring fault. */
        ESP_LOGE(TAG, "start-up failed: %s", esp_err_to_name(err));
        status_led_set(STATUS_ERROR);
    }
}
