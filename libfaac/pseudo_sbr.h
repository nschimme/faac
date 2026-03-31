/*
 * FAAC - Freeware Advanced Audio Coder
 *
 * pseudo_sbr.h - Encoder-side Pseudo Spectral Band Replication for AAC-LC
 *
 * After the MDCT, the encoder's bandwidth limiter leaves the upper spectrum
 * empty at low bitrates.  Pseudo-SBR fills that gap by copying and
 * noise-dithering the top of the coded region, then extending sfbn so
 * BlocQuant will quantize the synthesised bins.
 *
 * This is a purely encoder-side technique: the output is a standard AAC-LC
 * bitstream requiring no decoder changes.
 */

#ifndef PSEUDO_SBR_H
#define PSEUDO_SBR_H

#include <stdint.h>
#include "coder.h"
#include "faac_real.h"

/* Minimum extension in Hz worth applying. */
#define SBR_MIN_EXTENSION 500u

/*
 * Returns 1 if pseudo-SBR is useful for the given configuration:
 *   - bitRate > 0  (ABR mode; VBR already encodes full bandwidth)
 *   - naturalBW < 65% of Nyquist  (room to extend)
 */
int PseudoSBRShouldEnable(unsigned int naturalBW, unsigned long sampleRate,
                           unsigned long bitRate);

/*
 * Compute the adaptive SBR target bandwidth.
 * Returns: naturalBW + (Nyquist - naturalBW) * frac(bitRate), capped at 90% Nyquist.
 * The fraction decreases with bitRate so the target tracks CalcBandwidth
 * automatically when bandwidth tiers are re-tuned.
 */
unsigned int PseudoSBRTargetBW(unsigned int naturalBW, unsigned long sampleRate,
                                unsigned long bitRate);

/*
 * Apply pseudo-SBR to one channel's MDCT frequency buffer.
 * - Fills freq[] from baseBW up to targetBW with spectral patches.
 * - Extends coderInfo->sfbn and sfb_offset[] to cover targetBW.
 *
 * Must be called after AACstereo() and before BlocQuant().
 * Only operates on ONLY_LONG_WINDOW frames; returns immediately for short blocks.
 *
 * cb_width_long / num_cb_long come from hEncoder->srInfo.
 */
void PseudoSBR(CoderInfo *coderInfo, faac_real *freq,
               unsigned long sampleRate,
               unsigned int baseBW, unsigned int targetBW,
               unsigned long bitRate,
               const int *cb_width_long, int num_cb_long,
               uint32_t *randState);

#endif /* PSEUDO_SBR_H */
