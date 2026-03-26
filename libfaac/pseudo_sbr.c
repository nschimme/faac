/*
 * FAAC - Freeware Advanced Audio Coder
 *
 * pseudo_sbr.c - Encoder-side Pseudo Spectral Band Replication for AAC-LC
 *
 * Algorithm overview:
 *   1. Performs Spectral Patching: selects the top 50% of the existing coded
 *      bandwidth (the "source ceiling") and translates it up into the target
 *      extension region.
 *   2. Applies tuned parameters to make replication sound natural:
 *      - Gain Rolloff: ≈ -9dB/patch (0.354f) to simulate spectral decay.
 *      - Breathiness: 12% white noise injection + 0.005f comfort noise floor.
 *      - Spectral Cross-fade: 4-bin linear transition to prevent seams.
 *   3. Extends coderInfo->sfbn and sfb_offset[] so BlocQuant will quantize
 *      the synthesized bins.
 *
 * This is a purely encoder-side technique compatible with standard AAC decoders.
 */

#include <math.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#include "pseudo_sbr.h"
#include "coder.h"
#include "faac_real.h"

/* -------------------------------------------------------------------------
 * Tuning constants
 * ---------------------------------------------------------------------- */
#define MAX_SBR_PATCHES   4
#define SBR_PATCH_ROLLOFF 0.354f  /* ≈ –9 dB per subsequent patch           */
#define SBR_NOISE_FRAC    0.12f   /* 12% white noise injection              */
#define SBR_COMFORT_NOISE 0.005f  /* constant texture during silence        */
#define MIN_PATCH_BINS    8       /* don't bother with very small patches   */
#define SBR_XFADE_BINS    4       /* spectral cross-fade length             */

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

/* LCG PRNG — deterministic, fast, 0-overhead state is caller's uint32_t. */
static float lcg_float(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    /* Map high 16 bits to [-0.5, 0.5) */
    return (float)(*state >> 16) / 65536.0f - 0.5f;
}

/* Convert a bandwidth in Hz to an MDCT bin index. */
static int bw_to_bin(unsigned int bw_hz, unsigned long sampleRate, int frame_len)
{
    /* bin = bw_hz * frame_len / (sampleRate/2) = bw_hz * 2 * frame_len / sampleRate */
    return (int)((unsigned long long)bw_hz * 2ULL * (unsigned long)frame_len / sampleRate);
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

int PseudoSBRShouldEnable(unsigned int naturalBW, unsigned long sampleRate,
                           unsigned long bitRate)
{
    unsigned int nyquist;

    if (!bitRate)          /* VBR: naturalBW already = Nyquist */
        return 0;
    if (!sampleRate)
        return 0;

    nyquist = (unsigned int)(sampleRate / 2);
    if (!nyquist)
        return 0;

    /* Auto-enable if bitrate < 48kbps per channel */
    if (bitRate < 48000)
        return 1;

    /* Otherwise disable */
    return 0;
}

unsigned int PseudoSBRTargetBW(unsigned int naturalBW, unsigned long sampleRate,
                                unsigned long bitRate)
{
    unsigned int nyquist = (unsigned int)(sampleRate / 2);
    unsigned int gap;
    float frac;
    unsigned int target;
    unsigned int cap;

    if (!nyquist || naturalBW >= nyquist)
        return naturalBW;

    gap = nyquist - naturalBW;

    /*
     * Adaptive extension limits based on research summary:
     * < 12 kbps/ch: 15% expansion
     * < 24 kbps/ch: 25% expansion
     * < 48 kbps/ch: 40% expansion
     */
    if      (bitRate < 12000) frac = 0.15f;
    else if (bitRate < 24000) frac = 0.25f;
    else if (bitRate < 48000) frac = 0.40f;
    else                      frac = 0.0f;

    if (frac <= 0.0f)
        return naturalBW;

    target = naturalBW + (unsigned int)((float)gap * frac);

    /* Hard cap at 90% of Nyquist */
    cap = nyquist * 9u / 10u;
    return (target < cap) ? target : cap;
}

void PseudoSBR(CoderInfo *coderInfo, faac_real *freq,
               unsigned long sampleRate,
               unsigned int baseBW, unsigned int targetBW,
               unsigned long bitRate,
               const SR_INFO *srInfo,
               uint32_t *randState)
{
    int bw_bin, tgt_bin;
    float cumulative_gain;
    int dst, sb, offset;
    int patch, p;

    (void)bitRate; /* used by caller; not needed inside the patch loop */

    /* Only extend long-window blocks; short blocks are transients */
    if (coderInfo->block_type != ONLY_LONG_WINDOW)
        return;

    bw_bin  = bw_to_bin(baseBW,   sampleRate, FRAME_LEN);
    tgt_bin = bw_to_bin(targetBW, sampleRate, FRAME_LEN);

    /* Clamp tgt_bin to the last valid scale-factor band end */
    if (srInfo->num_cb_long > 0) {
        /* Walk the band table to find total bins covered */
        int max_bin = 0;
        for (sb = 0; sb < srInfo->num_cb_long; sb++)
            max_bin += srInfo->cb_width_long[sb];
        if (tgt_bin > max_bin)
            tgt_bin = max_bin;
    }

    /* Sanity: need at least MIN_PATCH_BINS of extension and a valid source */
    if (tgt_bin <= bw_bin + MIN_PATCH_BINS)
        return;
    if (bw_bin <= MIN_PATCH_BINS)
        return;

    /* ------------------------------------------------------------------
     * Patch loop: fill [bw_bin, tgt_bin) with copies of the source region.
     * Uses "Top-Half Translation" (Spectral Shifting) and cross-fading.
     * ------------------------------------------------------------------ */
    cumulative_gain = 1.0f;
    dst = bw_bin;

    for (patch = 0; patch < MAX_SBR_PATCHES && dst < tgt_bin; patch++) {
        int remaining   = tgt_bin - dst;
        /* Select top 50% of coded bandwidth as source (Spectral Shifting) */
        int src_size    = bw_bin / 2; 
        int patch_size  = (src_size < remaining) ? src_size : remaining;
        int src_start   = bw_bin - patch_size;
        float src_energy = 0.0f;
        float noise_scale;

        if (patch_size < MIN_PATCH_BINS)
            break;
        if (cumulative_gain < 0.01f)
            break;

        /* Measure source energy for noise normalization */
        for (p = 0; p < patch_size; p++)
            src_energy += (float)(freq[src_start + p] * freq[src_start + p]);

        noise_scale = (src_energy > 1e-15f)
            ? SBR_NOISE_FRAC * sqrtf(src_energy / (float)patch_size)
            : 0.0f;

        /* Copy patch with gain, dithered noise, and comfort noise */
        for (p = 0; p < patch_size; p++) {
            /* Spectral Translation: copy source bins directly */
            int src_idx = src_start + p;
            float s = (float)freq[src_idx] * cumulative_gain;
            float n = lcg_float(randState) * noise_scale;
            float c = lcg_float(randState) * SBR_COMFORT_NOISE;
            
            float val = s + n + c;

            /* Cross-fade at the very first crossover point */
            if (patch == 0 && p < SBR_XFADE_BINS) {
                float alpha = (float)(p + 1) / (float)(SBR_XFADE_BINS + 1);
                val = val * alpha + (float)freq[dst + p] * (1.0f - alpha);
            }

            freq[dst + p] = (faac_real)val;
        }

        dst             += patch_size;
        cumulative_gain *= SBR_PATCH_ROLLOFF;
    }

    /* Zero any remaining extension bins (avoids garbage from prior frame) */
    if (dst < tgt_bin)
        memset(freq + dst, 0, (size_t)(tgt_bin - dst) * sizeof(faac_real));

    /* ------------------------------------------------------------------
     * Extend sfbn and sfb_offset[] to cover tgt_bin.
     * ------------------------------------------------------------------ */
    sb     = coderInfo->sfbn;
    offset = coderInfo->sfb_offset[sb]; /* end sentinel from normal setup */

    while (sb < srInfo->num_cb_long && sb < NSFB_LONG && offset < tgt_bin) {
        coderInfo->sfb_offset[sb + 1] = offset + srInfo->cb_width_long[sb];
        offset = coderInfo->sfb_offset[sb + 1];
        sb++;
    }

    coderInfo->sfbn = sb;
}
