#include "echo.h"

#include <stdint.h>
#include <string.h>

#include "audio.h"
#include "dsp.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "status_led.h"
#include "vad.h"

static const char *TAG = "echo";

#define BLOCK_MS         20
#define BLOCK_SAMPLES    (AUDIO_SAMPLE_RATE_HZ * BLOCK_MS / 1000)
#define MS_TO_BLOCKS(ms) (((ms) + BLOCK_MS - 1) / BLOCK_MS)

#define CALIBRATION_BLOCKS MS_TO_BLOCKS(500)
#define PREROLL_BLOCKS     MS_TO_BLOCKS(CONFIG_VT_VAD_PREROLL_MS)
#define MIN_SPEECH_BLOCKS  MS_TO_BLOCKS(CONFIG_VT_VAD_MIN_SPEECH_MS)
#define HANG_BLOCKS        MS_TO_BLOCKS(CONFIG_VT_VAD_HANG_MS)
/* Trailing silence kept at the end of a recording. */
#define TAIL_KEEP_BLOCKS MS_TO_BLOCKS(200)
/* After playback the room still echoes; ignore the microphone for a moment. */
#define ECHO_GUARD_MS          300
#define PLAYBACK_CHUNK_SAMPLES (AUDIO_SAMPLE_RATE_HZ / 10)
#define IO_TIMEOUT_MS          1000

#define TASK_STACK_BYTES 4096
#define TASK_PRIORITY    5

typedef struct {
    int16_t *buf;
    size_t capacity; /* samples */
    size_t length;   /* samples */
    bool active;
} recording_t;

static vad_t s_vad;
static vad_preroll_t s_preroll;
static int16_t s_preroll_storage[(PREROLL_BLOCKS > 0 ? PREROLL_BLOCKS : 1) * BLOCK_SAMPLES];
static int16_t s_block[BLOCK_SAMPLES];
static recording_t s_rec;

static void listen(void)
{
    vTaskDelay(pdMS_TO_TICKS(ECHO_GUARD_MS));
    audio_mic_discard();
    vad_reset(&s_vad);
    s_rec.active = false;
    status_led_set(STATUS_LISTENING);
    ESP_LOGI(TAG, "listening (noise %.0f, threshold %.0f)", (double)s_vad.noise,
             (double)vad_start_threshold(&s_vad));
}

static void play(const int16_t *pcm, size_t samples)
{
    status_led_set(STATUS_PLAYING);
    ESP_LOGI(TAG, "playing %u ms", (unsigned)(samples * 1000 / AUDIO_SAMPLE_RATE_HZ));
    while (samples > 0) {
        const size_t n = samples < PLAYBACK_CHUNK_SAMPLES ? samples : PLAYBACK_CHUNK_SAMPLES;
        esp_err_t err = audio_speaker_write(pcm, n, IO_TIMEOUT_MS);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "speaker write: %s", esp_err_to_name(err));
            break;
        }
        esp_task_wdt_reset();
        pcm += n;
        samples -= n;
    }
    vTaskDelay(pdMS_TO_TICKS(audio_speaker_drain_ms()));
}

static void finish_recording(vad_event_t event, bool full)
{
    if (event == VAD_END && s_rec.length > (HANG_BLOCKS - TAIL_KEEP_BLOCKS) * BLOCK_SAMPLES) {
        s_rec.length -= (HANG_BLOCKS - TAIL_KEEP_BLOCKS) * BLOCK_SAMPLES;
    }

    const size_t blocks = s_rec.length / BLOCK_SAMPLES;
    if (blocks < PREROLL_BLOCKS + MIN_SPEECH_BLOCKS) {
        ESP_LOGI(TAG, "short sound ignored");
        return;
    }
    if (full) {
        ESP_LOGW(TAG, "recording buffer full");
    }
    ESP_LOGI(TAG, "recorded: peak %ld, RMS %.0f", (long)dsp_peak(s_rec.buf, s_rec.length),
             (double)dsp_rms(s_rec.buf, s_rec.length));
    play(s_rec.buf, s_rec.length);
}

static void echo_task(void *arg)
{
    (void)arg;
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    /* Learn the room noise floor before listening. */
    audio_mic_discard();
    for (int i = 0; i < CALIBRATION_BLOCKS; i++) {
        ESP_ERROR_CHECK(audio_mic_read(s_block, BLOCK_SAMPLES, IO_TIMEOUT_MS));
        vad_process(&s_vad, dsp_rms(s_block, BLOCK_SAMPLES));
    }
    listen();

    for (;;) {
        esp_task_wdt_reset();

        esp_err_t err = audio_mic_read(s_block, BLOCK_SAMPLES, IO_TIMEOUT_MS);
        if (err != ESP_OK) {
            /* No data from the microphone means a clock or wiring fault. */
            ESP_LOGE(TAG, "mic read: %s", esp_err_to_name(err));
            status_led_set(STATUS_ERROR);
            continue;
        }
        const vad_event_t event = vad_process(&s_vad, dsp_rms(s_block, BLOCK_SAMPLES));

        if (!s_rec.active) {
            vad_preroll_push(&s_preroll, s_block);
            if (event == VAD_START) {
                /* The current block is already in the pre-roll buffer. */
                s_rec.length = vad_preroll_drain(&s_preroll, s_rec.buf, s_rec.capacity);
                s_rec.active = true;
                status_led_set(STATUS_RECORDING);
                ESP_LOGI(TAG, "speech (level %.0f)", (double)s_vad.level);
            }
            continue;
        }

        const bool full = s_rec.length + BLOCK_SAMPLES > s_rec.capacity;
        if (!full) {
            memcpy(&s_rec.buf[s_rec.length], s_block, sizeof(s_block));
            s_rec.length += BLOCK_SAMPLES;
        }
        if (event == VAD_END || full) {
            finish_recording(event, full);
            listen();
        }
    }
}

esp_err_t echo_start(void)
{
    const vad_config_t vad_cfg = {
        .start_factor = (float)CONFIG_VT_VAD_START_FACTOR_X10 / 10.0f,
        .stop_factor = (float)CONFIG_VT_VAD_STOP_FACTOR_X10 / 10.0f,
        .min_level = (float)CONFIG_VT_VAD_MIN_LEVEL,
        .start_blocks = MS_TO_BLOCKS(CONFIG_VT_VAD_START_MS),
        .hang_blocks = HANG_BLOCKS,
    };
    vad_init(&s_vad, &vad_cfg);
    vad_preroll_init(&s_preroll, s_preroll_storage, BLOCK_SAMPLES, PREROLL_BLOCKS);

    s_rec.capacity = (size_t)AUDIO_SAMPLE_RATE_HZ * CONFIG_VT_ECHO_MAX_RECORD_MS / 1000;
    s_rec.capacity -= s_rec.capacity % BLOCK_SAMPLES;
    s_rec.buf =
        heap_caps_malloc(s_rec.capacity * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(s_rec.buf != NULL, ESP_ERR_NO_MEM, TAG, "no memory for %u ms recording",
                        (unsigned)CONFIG_VT_ECHO_MAX_RECORD_MS);

    BaseType_t ok = xTaskCreate(echo_task, "echo", TASK_STACK_BYTES, NULL, TASK_PRIORITY, NULL);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task create");
    return ESP_OK;
}
