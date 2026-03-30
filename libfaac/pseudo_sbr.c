/*
 * FAAC - Freeware Advanced Audio Coder
 *
 * pseudo_sbr.c - Encoder-side Pseudo Spectral Band Replication for AAC-LC
 *
 * Algorithm overview:
 *   1. Compute SFM (Spectral Flatness Measure) of the top coded region to
 *      drive adaptive noise injection (tonal → less noise, noisy → more).
 *   2. Copy spectral patches from just below the natural bandwidth into the
 *      empty region above it, applying a cumulative −6 dB/patch gain rolloff
 *      and per-bin noise dithering.
 *   3. Extend coderInfo->sfbn and sfb_offset[] so BlocQuant will quantize
 *      the synthesised bins.
 *
 * The PRNG (LCG) is seeded once at encoder open from the sample rate, so
 * re-encoding the same file produces a bit-identical bitstream (stable MD5).
 */

#include <math.h>
#include <string.h>
#include <stdint.h>

#include "pseudo_sbr.h"
#include "coder.h"
#include "faac_real.h"

/* -------------------------------------------------------------------------
 * Tuning constants
 * ---------------------------------------------------------------------- */
#define MAX_SBR_PATCHES   4
#define SBR_PATCH_ROLLOFF 0.50f   /* –6 dB gain per subsequent patch       */
#define SBR_FILL_THRESH   65u     /* disable if naturalBW > 65% of Nyquist */
#define SBR_NOISE_OFFSET  0.05f   /* minimum noise injection fraction       */
#define SBR_NOISE_SLOPE   0.20f   /* noise scales by (1-SFM) * slope        */
#define SBR_SFM_WINDOW    64      /* bins below bw_bin used for SFM         */
#define MIN_PATCH_BINS    8       /* don't bother with very small patches   */

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

/*
 * Spectral Flatness Measure over freq[start..end).
 * Returns geometric/arithmetic mean of squared magnitudes.
 * 0.0 = perfectly tonal, 1.0 = flat noise.
 */
static float compute_sfm(const faac_real *freq, int start, int end)
{
    float log_sum = 0.0f;
    float lin_sum = 0.0f;
    const float eps = 1e-12f;
    int n = end - start;
    int i;

    if (n <= 0)
        return 1.0f;

    for (i = start; i < end; i++) {
        float e = (float)(freq[i] * freq[i]) + eps;
        log_sum += logf(e);
        lin_sum += e;
    }

    float geo_mean  = expf(log_sum / (float)n);
    float arith_mean = lin_sum / (float)n;

    return (arith_mean > 1e-20f) ? (geo_mean / arith_mean) : 1.0f;
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

    /* Enable only when natural bandwidth leaves a meaningful gap */
    return (naturalBW * 100u / nyquist) < SBR_FILL_THRESH;
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
     * Extension fraction: how much of the gap to fill.
     * Derived from bitRate so it scales automatically as CalcBandwidth()
     * is re-tuned — no hard-coded Hz targets.
     */
    if      (bitRate <= 16000) frac = 0.45f;
    else if (bitRate <= 32000) frac = 0.35f;
    else if (bitRate <= 48000) frac = 0.25f;
    else if (bitRate <= 64000) frac = 0.15f;
    else                       frac = 0.0f;

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
               const int *cb_width_long, int num_cb_long,
               uint32_t *randState)
{
    int bw_bin, tgt_bin;
    int sfm_start;
    float sfm, noise_mix;
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
    if (num_cb_long > 0) {
        /* Walk the band table to find total bins covered */
        int max_bin = 0;
        for (sb = 0; sb < num_cb_long && sb < NSFB_LONG; sb++)
            max_bin += cb_width_long[sb];
        if (tgt_bin > max_bin)
            tgt_bin = max_bin;
    }

    /* Sanity: need at least MIN_PATCH_BINS of extension and a valid source */
    if (tgt_bin <= bw_bin + MIN_PATCH_BINS)
        return;
    if (bw_bin <= MIN_PATCH_BINS)
        return;

    /* ------------------------------------------------------------------
     * Compute SFM of the SBR_SFM_WINDOW bins just below the bandwidth.
     * More tonal content → less noise injection (preserve spectral shape).
     * ------------------------------------------------------------------ */
    sfm_start = bw_bin - SBR_SFM_WINDOW;
    if (sfm_start < 0) sfm_start = 0;
    sfm = compute_sfm(freq, sfm_start, bw_bin);
    /* sfm in [0,1]: 0=pure tone → minimum noise, 1=flat → maximum noise */
    noise_mix = SBR_NOISE_OFFSET + (1.0f - sfm) * SBR_NOISE_SLOPE;

    /* ------------------------------------------------------------------
     * Patch loop: fill [bw_bin, tgt_bin) with copies of the source region.
     * Each successive patch is attenuated by SBR_PATCH_ROLLOFF (≈ −6 dB).
     * ------------------------------------------------------------------ */
    cumulative_gain = 1.0f;
    dst = bw_bin;

    for (patch = 0; patch < MAX_SBR_PATCHES && dst < tgt_bin; patch++) {
        int remaining   = tgt_bin - dst;
        int src_size    = bw_bin; /* can borrow up to all coded bins */
        int patch_size  = (src_size < remaining) ? src_size : remaining;
        int src_start   = bw_bin - patch_size; /* top of coded region */
        float src_energy = 0.0f;
        float noise_scale;

        if (patch_size < MIN_PATCH_BINS)
            break;
        if (cumulative_gain < 0.05f)
            break;

        /* Measure source energy for noise normalisation */
        for (p = 0; p < patch_size; p++)
            src_energy += (float)(freq[src_start + p] * freq[src_start + p]);

        noise_scale = (src_energy > 1e-15f)
            ? noise_mix * sqrtf(src_energy / (float)patch_size)
            : 0.0f;

        /* Copy patch with gain + dithered noise */
        for (p = 0; p < patch_size; p++) {
            float s = (float)freq[src_start + p] * cumulative_gain;
            float n = lcg_float(randState) * noise_scale;
            freq[dst + p] = (faac_real)(s + n);
        }

        dst             += patch_size;
        cumulative_gain *= SBR_PATCH_ROLLOFF;
    }

    /* Zero any remaining extension bins (avoids garbage from prior frame) */
    if (dst < tgt_bin)
        memset(freq + dst, 0, (size_t)(tgt_bin - dst) * sizeof(faac_real));

    /* ------------------------------------------------------------------
     * Extend sfbn and sfb_offset[] to cover tgt_bin.
     *
     * After the per-channel sfb setup in frame.c, sfb_offset[sfbn] holds
     * the end-sentinel (= total coded bins = first new-band start).
     * We walk the srInfo band-width table from the current sfbn forward
     * until we pass tgt_bin.
     * ------------------------------------------------------------------ */
    sb     = coderInfo->sfbn;
    offset = coderInfo->sfb_offset[sb]; /* end sentinel from normal setup */

    while (sb < num_cb_long && offset < tgt_bin) {
        coderInfo->sfb_offset[sb + 1] = offset + cb_width_long[sb];
        offset = coderInfo->sfb_offset[sb + 1];
        sb++;
    }

    coderInfo->sfbn = sb;
}
