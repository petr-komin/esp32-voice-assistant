#include "audio.h"

#include "driver/i2s_std.h"
#include "dsp.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "audio";

/* 20 ms per DMA frame at 16 kHz; 6 frames give 120 ms of slack. */
#define DMA_FRAME_SAMPLES 320
#define DMA_DESC_NUM      6

/* Raw 32-bit words are converted in chunks of this size. */
#define MIC_CHUNK_SAMPLES DMA_FRAME_SAMPLES

static i2s_chan_handle_t s_mic;
static i2s_chan_handle_t s_speaker;
static unsigned s_gain_shift;
static dsp_dc_blocker_t s_dc;
static int32_t s_raw[MIC_CHUNK_SAMPLES];

esp_err_t audio_mic_init(const audio_mic_config_t *cfg)
{
    ESP_RETURN_ON_FALSE(s_mic == NULL, ESP_ERR_INVALID_STATE, TAG,
                        "microphone already initialised");
    ESP_RETURN_ON_FALSE(cfg->gain_shift < 32, ESP_ERR_INVALID_ARG, TAG, "gain_shift out of range");

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = DMA_DESC_NUM;
    chan_cfg.dma_frame_num = DMA_FRAME_SAMPLES;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, NULL, &s_mic), TAG, "mic channel");

    /* The INMP441 sends 24-bit data in 32-bit slots, Philips (standard I2S) framing. */
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE_HZ),
        .slot_cfg =
            I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg =
            {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = cfg->bclk_gpio,
                .ws = cfg->ws_gpio,
                .dout = I2S_GPIO_UNUSED,
                .din = cfg->data_gpio,
            },
    };
    std_cfg.slot_cfg.slot_mask = cfg->right_slot ? I2S_STD_SLOT_RIGHT : I2S_STD_SLOT_LEFT;

    esp_err_t err = i2s_channel_init_std_mode(s_mic, &std_cfg);
    if (err == ESP_OK) {
        err = i2s_channel_enable(s_mic);
    }
    if (err != ESP_OK) {
        i2s_del_channel(s_mic);
        s_mic = NULL;
        ESP_LOGE(TAG, "mic init failed: %s", esp_err_to_name(err));
        return err;
    }

    s_gain_shift = cfg->gain_shift;
    dsp_dc_blocker_reset(&s_dc);
    ESP_LOGI(TAG, "mic: BCLK %d, WS %d, SD %d, %s slot, gain shift %u", cfg->bclk_gpio,
             cfg->ws_gpio, cfg->data_gpio, cfg->right_slot ? "right" : "left", s_gain_shift);
    return ESP_OK;
}

esp_err_t audio_mic_read(int16_t *out, size_t samples, uint32_t timeout_ms)
{
    ESP_RETURN_ON_FALSE(s_mic != NULL, ESP_ERR_INVALID_STATE, TAG, "microphone not initialised");

    while (samples > 0) {
        size_t want = samples < MIC_CHUNK_SAMPLES ? samples : MIC_CHUNK_SAMPLES;
        size_t got_bytes = 0;
        esp_err_t err =
            i2s_channel_read(s_mic, s_raw, want * sizeof(int32_t), &got_bytes, timeout_ms);
        if (err != ESP_OK) {
            return err;
        }
        size_t got = got_bytes / sizeof(int32_t);
        dsp_mic_to_pcm16(s_raw, out, got, s_gain_shift, &s_dc);
        out += got;
        samples -= got;
    }
    return ESP_OK;
}

void audio_mic_discard(void)
{
    if (s_mic == NULL) {
        return;
    }
    size_t got_bytes;
    do {
        got_bytes = 0;
        (void)i2s_channel_read(s_mic, s_raw, sizeof(s_raw), &got_bytes, 0);
    } while (got_bytes > 0);
}

esp_err_t audio_speaker_init(const audio_speaker_config_t *cfg)
{
    ESP_RETURN_ON_FALSE(s_speaker == NULL, ESP_ERR_INVALID_STATE, TAG,
                        "speaker already initialised");

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = DMA_DESC_NUM;
    chan_cfg.dma_frame_num = DMA_FRAME_SAMPLES;
    chan_cfg.auto_clear_after_cb = true; /* play silence once the queue runs dry */
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_speaker, NULL), TAG, "speaker channel");

    /* Mono data sent to both slots; the MAX98357A mixes (L+R)/2 by default. */
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE_HZ),
        .slot_cfg =
            I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg =
            {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = cfg->bclk_gpio,
                .ws = cfg->ws_gpio,
                .dout = cfg->data_gpio,
                .din = I2S_GPIO_UNUSED,
            },
    };
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;

    esp_err_t err = i2s_channel_init_std_mode(s_speaker, &std_cfg);
    if (err == ESP_OK) {
        err = i2s_channel_enable(s_speaker);
    }
    if (err != ESP_OK) {
        i2s_del_channel(s_speaker);
        s_speaker = NULL;
        ESP_LOGE(TAG, "speaker init failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "speaker: BCLK %d, LRC %d, DIN %d", cfg->bclk_gpio, cfg->ws_gpio, cfg->data_gpio);
    return ESP_OK;
}

esp_err_t audio_speaker_write(const int16_t *pcm, size_t samples, uint32_t timeout_ms)
{
    ESP_RETURN_ON_FALSE(s_speaker != NULL, ESP_ERR_INVALID_STATE, TAG, "speaker not initialised");

    size_t written = 0;
    esp_err_t err =
        i2s_channel_write(s_speaker, pcm, samples * sizeof(int16_t), &written, timeout_ms);
    if (err == ESP_OK && written != samples * sizeof(int16_t)) {
        err = ESP_ERR_TIMEOUT;
    }
    return err;
}

uint32_t audio_speaker_drain_ms(void)
{
    return (uint32_t)(DMA_DESC_NUM * DMA_FRAME_SAMPLES * 1000 / AUDIO_SAMPLE_RATE_HZ);
}
