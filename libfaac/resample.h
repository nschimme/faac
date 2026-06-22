/*
 * FAAC - Freeware Advanced Audio Coder
 *
 * 2:1 FIR downsampler for HE-AAC core signal preparation.
 * Takes full-rate PCM (Fs) and produces half-rate PCM (Fs/2)
 * for the AAC-LC core encoder.
 */

#ifndef RESAMPLE_H
#define RESAMPLE_H

#include "faac_real.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 63-tap equiripple half-band FIR, -6 dB at Fs/4. Minimum length for ≥60 dB alias
 * rejection in [Fs/4..Fs/2]; SBR crossover at 7-9.5 kHz is well inside the passband. */
#define RESAMPLE_FILTER_LEN 63

typedef struct Resampler {
    faac_real  buf[MAX_CHANNELS][RESAMPLE_FILTER_LEN]; /* per-channel history */
    int        channels;
} Resampler;

/**
 * ResampleOpen - allocate and zero-initialise a Resampler.
 */
Resampler *ResampleOpen(int channels);

/** ResampleClose - free a Resampler allocated by ResampleOpen. */
void ResampleClose(Resampler *r);

/**
 * Resample2to1 - downsample by 2 with anti-alias FIR.
 *
 * @r         : resampler state (updated in-place)
 * @input     : per-channel input arrays, each of length input_len
 * @input_len : number of full-rate samples per channel (= FRAME_LEN)
 * @output    : per-channel output arrays, each of length input_len/2
 *
 * Returns number of output samples produced per channel (= input_len/2).
 */
int Resample2to1(Resampler *r,
                 faac_real *input[MAX_CHANNELS],
                 int input_len,
                 faac_real *output[MAX_CHANNELS]);

#ifdef __cplusplus
}
#endif

#endif /* RESAMPLE_H */
