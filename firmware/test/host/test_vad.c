#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "vad.h"

void setUp(void) {}
void tearDown(void) {}

#define NOISE  200.0f
#define SPEECH 3000.0f

static vad_t make_vad(void)
{
    const vad_config_t cfg = VAD_CONFIG_DEFAULT();
    vad_t vad;
    vad_init(&vad, &cfg);
    for (int i = 0; i < 25; i++) {
        TEST_ASSERT_EQUAL(VAD_SILENCE, vad_process(&vad, NOISE));
    }
    return vad;
}

static void test_learns_noise_floor(void)
{
    vad_t vad = make_vad();
    TEST_ASSERT_FLOAT_WITHIN(1.0f, NOISE, vad.noise);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 600.0f, vad_start_threshold(&vad));
}

static void test_min_level_applies_in_quiet_room(void)
{
    const vad_config_t cfg = VAD_CONFIG_DEFAULT();
    vad_t vad;
    vad_init(&vad, &cfg);
    vad_process(&vad, 10.0f);
    TEST_ASSERT_EQUAL_FLOAT(500.0f, vad_start_threshold(&vad));
}

static void test_speech_starts_after_start_blocks(void)
{
    vad_t vad = make_vad();
    TEST_ASSERT_EQUAL(VAD_SILENCE, vad_process(&vad, SPEECH));
    TEST_ASSERT_EQUAL(VAD_SILENCE, vad_process(&vad, SPEECH));
    TEST_ASSERT_EQUAL(VAD_START, vad_process(&vad, SPEECH));
    TEST_ASSERT_EQUAL(VAD_SPEECH, vad_process(&vad, SPEECH));
}

static void test_short_click_does_not_start(void)
{
    vad_t vad = make_vad();
    vad_process(&vad, SPEECH);
    vad_process(&vad, SPEECH);
    TEST_ASSERT_EQUAL(VAD_SILENCE, vad_process(&vad, NOISE));
    TEST_ASSERT_EQUAL(VAD_SILENCE, vad_process(&vad, SPEECH));
    TEST_ASSERT_EQUAL(VAD_SILENCE, vad_process(&vad, SPEECH));
    TEST_ASSERT_EQUAL(VAD_START, vad_process(&vad, SPEECH));
}

static void test_speech_ends_after_hang_time(void)
{
    vad_t vad = make_vad();
    for (int i = 0; i < 3; i++) {
        vad_process(&vad, SPEECH);
    }
    for (int i = 0; i < 39; i++) {
        TEST_ASSERT_EQUAL(VAD_SPEECH, vad_process(&vad, NOISE));
    }
    TEST_ASSERT_EQUAL(VAD_END, vad_process(&vad, NOISE));
    TEST_ASSERT_EQUAL(VAD_SILENCE, vad_process(&vad, NOISE));
}

static void test_pause_inside_speech_keeps_recording(void)
{
    vad_t vad = make_vad();
    for (int i = 0; i < 3; i++) {
        vad_process(&vad, SPEECH);
    }
    for (int i = 0; i < 30; i++) {
        vad_process(&vad, NOISE);
    }
    TEST_ASSERT_EQUAL(VAD_SPEECH, vad_process(&vad, SPEECH)); /* resets the hang counter */
    for (int i = 0; i < 39; i++) {
        TEST_ASSERT_EQUAL(VAD_SPEECH, vad_process(&vad, NOISE));
    }
    TEST_ASSERT_EQUAL(VAD_END, vad_process(&vad, NOISE));
}

static void test_noise_floor_rises_slowly(void)
{
    vad_t vad = make_vad();
    /* A steady fan at 1.5x the old floor is below the start threshold ... */
    for (int i = 0; i < 500; i++) {
        TEST_ASSERT_EQUAL(VAD_SILENCE, vad_process(&vad, 300.0f));
    }
    /* ... and after 10 s it has become the new floor. */
    TEST_ASSERT_FLOAT_WITHIN(5.0f, 300.0f, vad.noise);
}

static void test_reset_keeps_noise_floor(void)
{
    vad_t vad = make_vad();
    for (int i = 0; i < 3; i++) {
        vad_process(&vad, SPEECH);
    }
    vad_reset(&vad);
    TEST_ASSERT_FALSE(vad.speaking);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, NOISE, vad.noise);
}

/* --- pre-roll ----------------------------------------------------------- */

#define BS 4 /* samples per block */

static void fill_block(int16_t *block, int16_t value)
{
    for (int i = 0; i < BS; i++) {
        block[i] = value;
    }
}

static void test_preroll_returns_blocks_oldest_first(void)
{
    int16_t storage[3 * BS];
    vad_preroll_t pr;
    vad_preroll_init(&pr, storage, BS, 3);

    int16_t block[BS];
    for (int16_t v = 1; v <= 5; v++) { /* 5 pushes into 3 slots: 3, 4, 5 remain */
        fill_block(block, v);
        vad_preroll_push(&pr, block);
    }

    int16_t out[3 * BS];
    TEST_ASSERT_EQUAL_size_t(3 * BS, vad_preroll_drain(&pr, out, 3 * BS));
    TEST_ASSERT_EQUAL_INT16(3, out[0]);
    TEST_ASSERT_EQUAL_INT16(4, out[BS]);
    TEST_ASSERT_EQUAL_INT16(5, out[2 * BS + BS - 1]);
    TEST_ASSERT_EQUAL_size_t(0, vad_preroll_drain(&pr, out, 3 * BS));
}

static void test_preroll_partial_fill(void)
{
    int16_t storage[3 * BS];
    vad_preroll_t pr;
    vad_preroll_init(&pr, storage, BS, 3);

    int16_t block[BS];
    fill_block(block, 7);
    vad_preroll_push(&pr, block);

    int16_t out[3 * BS];
    TEST_ASSERT_EQUAL_size_t(BS, vad_preroll_drain(&pr, out, 3 * BS));
    TEST_ASSERT_EQUAL_INT16(7, out[0]);
}

static void test_preroll_small_output_keeps_newest(void)
{
    int16_t storage[3 * BS];
    vad_preroll_t pr;
    vad_preroll_init(&pr, storage, BS, 3);

    int16_t block[BS];
    for (int16_t v = 1; v <= 3; v++) {
        fill_block(block, v);
        vad_preroll_push(&pr, block);
    }

    int16_t out[2 * BS + 1]; /* room for two whole blocks only */
    memset(out, 0, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(2 * BS, vad_preroll_drain(&pr, out, 2 * BS + 1));
    TEST_ASSERT_EQUAL_INT16(2, out[0]);
    TEST_ASSERT_EQUAL_INT16(3, out[BS]);
    TEST_ASSERT_EQUAL_INT16(0, out[2 * BS]);
}

static void test_preroll_zero_capacity(void)
{
    vad_preroll_t pr;
    vad_preroll_init(&pr, NULL, BS, 0);
    const int16_t block[BS] = {0};
    vad_preroll_push(&pr, block);
    int16_t out[BS];
    TEST_ASSERT_EQUAL_size_t(0, vad_preroll_drain(&pr, out, BS));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_learns_noise_floor);
    RUN_TEST(test_min_level_applies_in_quiet_room);
    RUN_TEST(test_speech_starts_after_start_blocks);
    RUN_TEST(test_short_click_does_not_start);
    RUN_TEST(test_speech_ends_after_hang_time);
    RUN_TEST(test_pause_inside_speech_keeps_recording);
    RUN_TEST(test_noise_floor_rises_slowly);
    RUN_TEST(test_reset_keeps_noise_floor);
    RUN_TEST(test_preroll_returns_blocks_oldest_first);
    RUN_TEST(test_preroll_partial_fill);
    RUN_TEST(test_preroll_small_output_keeps_newest);
    RUN_TEST(test_preroll_zero_capacity);
    return UNITY_END();
}
