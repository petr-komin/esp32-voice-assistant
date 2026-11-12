#include <math.h>
#include <stdint.h>

#include "dsp.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_rms_of_empty_block_is_zero(void)
{
    TEST_ASSERT_EQUAL_FLOAT(0.0f, dsp_rms(NULL, 0));
}

static void test_rms_of_constant_block(void)
{
    const int16_t block[4] = {-1000, 1000, -1000, 1000};
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1000.0f, dsp_rms(block, 4));
}

static void test_rms_of_full_scale_sine(void)
{
    int16_t block[1600];
    for (int i = 0; i < 1600; i++) {
        block[i] = (int16_t)(32767.0f * sinf(2.0f * 3.14159265f * 1000.0f * (float)i / 16000.0f));
    }
    /* RMS of a sine is peak / sqrt(2). */
    TEST_ASSERT_FLOAT_WITHIN(5.0f, 23170.0f, dsp_rms(block, 1600));
}

static void test_peak_handles_int16_min(void)
{
    const int16_t block[3] = {5, INT16_MIN, 100};
    TEST_ASSERT_EQUAL_INT32(32768, dsp_peak(block, 3));
}

static void test_mic_conversion_removes_dc(void)
{
    /* A constant offset (typical for MEMS microphones) must decay to ~0. */
    int32_t raw[4000];
    int16_t pcm[4000];
    for (int i = 0; i < 4000; i++) {
        raw[i] = 0x00100000; /* 2^20, i.e. 16 after a shift of 16 */
    }
    dsp_dc_blocker_t dc;
    dsp_dc_blocker_reset(&dc);
    dsp_mic_to_pcm16(raw, pcm, 4000, 16, &dc);
    TEST_ASSERT_INT16_WITHIN(1, 0, pcm[3999]);
}

static void test_mic_conversion_applies_gain(void)
{
    /* Alternating +-A passes the high-pass filter almost unchanged. */
    int32_t raw[64];
    int16_t pcm[64];
    for (int i = 0; i < 64; i++) {
        raw[i] = (i % 2) ? -(1 << 24) : (1 << 24);
    }
    dsp_dc_blocker_t dc;
    dsp_dc_blocker_reset(&dc);
    dsp_mic_to_pcm16(raw, pcm, 64, 14, &dc); /* 2^24 / 2^14 = 1024 */
    TEST_ASSERT_INT16_WITHIN(30, -1024, pcm[63]);
}

static void test_mic_conversion_saturates(void)
{
    const int32_t raw[2] = {INT32_MAX, INT32_MIN};
    int16_t pcm[2];
    dsp_dc_blocker_t dc;
    dsp_dc_blocker_reset(&dc);
    dsp_mic_to_pcm16(raw, pcm, 2, 8, &dc);
    TEST_ASSERT_EQUAL_INT16(INT16_MAX, pcm[0]);
    TEST_ASSERT_EQUAL_INT16(INT16_MIN, pcm[1]);
}

static void test_mic_conversion_in_place(void)
{
    int32_t buf[8] = {1 << 20, -(1 << 20), 1 << 20, -(1 << 20),
                      1 << 20, -(1 << 20), 1 << 20, -(1 << 20)};
    int16_t expected[8];
    dsp_dc_blocker_t dc;
    dsp_dc_blocker_reset(&dc);
    dsp_mic_to_pcm16(buf, expected, 8, 16, &dc);

    dsp_dc_blocker_reset(&dc);
    dsp_mic_to_pcm16(buf, (int16_t *)buf, 8, 16, &dc);
    TEST_ASSERT_EQUAL_INT16_ARRAY(expected, (int16_t *)buf, 8);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_rms_of_empty_block_is_zero);
    RUN_TEST(test_rms_of_constant_block);
    RUN_TEST(test_rms_of_full_scale_sine);
    RUN_TEST(test_peak_handles_int16_min);
    RUN_TEST(test_mic_conversion_removes_dc);
    RUN_TEST(test_mic_conversion_applies_gain);
    RUN_TEST(test_mic_conversion_saturates);
    RUN_TEST(test_mic_conversion_in_place);
    return UNITY_END();
}
