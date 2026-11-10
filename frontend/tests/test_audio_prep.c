/*
 * FAAC - Freeware Advanced Audio Coder
 * Unit test for audio preprocessing module
 */

#include "audio_prep.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TEST_SAMPLES_LEN 4800

static void test_prep_config_defaults(void)
{
    audio_prep_config_t cfg;
    audio_prep_config_init(&cfg);
    assert(cfg.mode == AUDIO_PREP_OFF);
    assert(cfg.shelf_gain_db < 0.0f);
    assert(cfg.transient_tame_db < 0.0f);
    assert(cfg.mono_cutoff_hz < 0.0f);
    assert(cfg.stereo_mid_narrow < 0.0f);
    assert(cfg.dynamics_amount < 0.0f);
}

static void test_high_shelf_preemphasis(void)
{
    audio_prep_config_t cfg;
    audio_prep_config_init(&cfg);
    cfg.mode = AUDIO_PREP_ON;
    cfg.shelf_gain_db = 3.0f;
    cfg.transient_tame_db = 0.0f;
    cfg.mono_cutoff_hz = 0.0f;
    cfg.stereo_mid_narrow = 0.0f;
    cfg.dynamics_amount = 0.0f;

    uint32_t rate = 48000;
    audio_prep_pipeline_t *pipe = audio_prep_create(&cfg, rate, 1, 16000);
    assert(pipe != NULL);

    const size_t N = TEST_SAMPLES_LEN;
    float low_tone[TEST_SAMPLES_LEN];
    float high_tone[TEST_SAMPLES_LEN];
    for (size_t i = 0; i < N; i++) {
        low_tone[i] = 0.2f * sinf(2.0f * (float)M_PI * 1000.0f * (float)i / (float)rate);
        high_tone[i] = 0.2f * sinf(2.0f * (float)M_PI * 8000.0f * (float)i / (float)rate);
    }

    audio_prep_process(pipe, low_tone, N);
    audio_prep_free(pipe);

    pipe = audio_prep_create(&cfg, rate, 1, 16000);
    audio_prep_process(pipe, high_tone, N);
    audio_prep_free(pipe);

    float rms_low = 0.0f, rms_high = 0.0f;
    for (size_t i = 2400; i < N; i++) {
        rms_low += low_tone[i] * low_tone[i];
        rms_high += high_tone[i] * high_tone[i];
    }
    rms_low = sqrtf(rms_low / (float)(N - 2400));
    rms_high = sqrtf(rms_high / (float)(N - 2400));

    float boost_db = 20.0f * log10f(rms_high / rms_low);
    (void)boost_db;
    assert(boost_db > 2.0f && boost_db < 4.0f);
}

static void test_bass_mono_summing(void)
{
    audio_prep_config_t cfg;
    audio_prep_config_init(&cfg);
    cfg.mode = AUDIO_PREP_ON;
    cfg.shelf_gain_db = 0.0f;
    cfg.transient_tame_db = 0.0f;
    cfg.mono_cutoff_hz = 250.0f;
    cfg.stereo_mid_narrow = 0.0f;
    cfg.dynamics_amount = 0.0f;

    uint32_t rate = 48000;
    audio_prep_pipeline_t *pipe = audio_prep_create(&cfg, rate, 2, 16000);
    assert(pipe != NULL);

    const size_t N = TEST_SAMPLES_LEN;
    float samples[TEST_SAMPLES_LEN * 2];
    for (size_t i = 0; i < N; i++) {
        samples[i * 2 + 0] = 0.5f * sinf(2.0f * (float)M_PI * 100.0f * (float)i / (float)rate);
        samples[i * 2 + 1] = 0.0f;
    }

    audio_prep_process(pipe, samples, N);
    audio_prep_free(pipe);

    float rms_left = 0.0f, rms_right = 0.0f;
    for (size_t i = 2400; i < N; i++) {
        rms_left += samples[i * 2 + 0] * samples[i * 2 + 0];
        rms_right += samples[i * 2 + 1] * samples[i * 2 + 1];
    }
    rms_left = sqrtf(rms_left / (float)(N - 2400));
    rms_right = sqrtf(rms_right / (float)(N - 2400));

    (void)rms_left;
    (void)rms_right;
    assert(rms_right > 0.10f);
    assert(fabsf(rms_left - rms_right) < 0.15f);
}

static void test_micro_transient_softening(void)
{
    audio_prep_config_t cfg;
    audio_prep_config_init(&cfg);
    cfg.mode = AUDIO_PREP_ON;
    cfg.shelf_gain_db = 0.0f;
    cfg.transient_tame_db = 3.0f;
    cfg.mono_cutoff_hz = 0.0f;
    cfg.stereo_mid_narrow = 0.0f;
    cfg.dynamics_amount = 0.0f;

    uint32_t rate = 48000;
    audio_prep_pipeline_t *pipe = audio_prep_create(&cfg, rate, 1, 16000);
    assert(pipe != NULL);

    const size_t N = TEST_SAMPLES_LEN;
    float samples[TEST_SAMPLES_LEN];
    for (size_t i = 0; i < N; i++) {
        samples[i] = 0.05f * sinf(2.0f * (float)M_PI * 1000.0f * (float)i / (float)rate);
    }
    samples[1000] = 0.90f;
    samples[1001] = 0.85f;

    audio_prep_process(pipe, samples, N);
    audio_prep_free(pipe);

    float peak = fabsf(samples[1000]);
    (void)peak;
    assert(peak < 0.80f);
}

int main(void)
{
    test_prep_config_defaults();
    test_high_shelf_preemphasis();
    test_bass_mono_summing();
    test_micro_transient_softening();
    printf("All audio_prep unit tests passed!\n");
    return 0;
}
