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
#include "coder.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 63-tap equiripple half-band FIR, -6 dB at Fs/4. Minimum length for ≥60 dB alias
 * rejection in [Fs/4..Fs/2]; SBR crossover at 7-9.5 kHz is well inside the passband. */
#define RESAMPLE_FILTER_LEN 63

typedef struct Resampler {
    faac_real  buf     [MAX_CHANNELS][RESAMPLE_FILTER_LEN]; /* FIR overlap state (carries between frames) */
    faac_real  fullRate[MAX_CHANNELS][2 * FRAME_LEN];       /* full-rate input: caller fills, SBR reads, FIR consumes */
    faac_real  halfRate[MAX_CHANNELS][FRAME_LEN];           /* downsampled output: written by Resample2to1 */
    int        channels;
} Resampler;

Resampler *ResampleOpen(int channels);
void ResampleClose(Resampler *r);

/* Resample2to1 - 2:1 downsample from r->fullRate into r->halfRate.
 * input_len: full-rate samples per channel (≤ 2*FRAME_LEN).
 * Returns output samples produced per channel (= input_len/2). */
int Resample2to1(Resampler *r, int input_len);

#ifdef __cplusplus
}
#endif

#endif /* RESAMPLE_H */
