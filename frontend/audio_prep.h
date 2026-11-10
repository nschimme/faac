/*
 * FAAC - Freeware Advanced Audio Coder
 * Audio Preprocessing Pipeline (xHE-AAC inspired low-bitrate acoustic prep)
 */

#ifndef AUDIO_PREP_H
#define AUDIO_PREP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUDIO_PREP_OFF = 0,
    AUDIO_PREP_AUTO = 1,
    AUDIO_PREP_ON = 2
} audio_prep_mode_t;

typedef struct {
    audio_prep_mode_t mode;

    /* Manual overrides (-1.0f means auto-scale based on bitrate) */
    float shelf_gain_db;      /* High-shelf boost gain (e.g., 0.0 to +4.0 dB) */
    float transient_tame_db;  /* Micro-transient reduction (e.g., 0.0 to 3.0 dB) */
    float mono_cutoff_hz;     /* Bass mono cutoff freq (e.g., 0.0 to 300.0 Hz) */
    float stereo_mid_narrow;  /* Mid-range stereo narrow factor (e.g., 0.0 to 0.3) */
    float dynamics_target_db; /* Target LUFS / RMS compression target (e.g., -18.0) */
    float dynamics_amount;    /* Upward compression mix/strength (0.0 to 1.0) */
} audio_prep_config_t;

typedef struct audio_prep_pipeline audio_prep_pipeline_t;

/* Initialize default config */
void audio_prep_config_init(audio_prep_config_t *cfg);

/* Create preprocessing pipeline.
 * sample_rate: audio sample rate (Hz)
 * channels: number of channels (1 to 8)
 * bitrate_per_channel: nominal bps per channel (e.g. 24000)
 */
audio_prep_pipeline_t *audio_prep_create(const audio_prep_config_t *cfg,
                                         uint32_t sample_rate,
                                         uint16_t channels,
                                         uint32_t bitrate_per_channel,
                                         bool is_ps);

/* Process interleaved float32 samples in place */
void audio_prep_process(audio_prep_pipeline_t *pipe,
                        float *samples,
                        size_t num_frames);

/* Free pipeline state */
void audio_prep_free(audio_prep_pipeline_t *pipe);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_PREP_H */
