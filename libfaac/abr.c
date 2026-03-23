/****************************************************************************
    ABR rate control functions

    Copyright (C) 2001 Menno Bakker

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
****************************************************************************/

#include "abr.h"
#include "coder.h"

void AbrInit(abr_t *abr, int bitRate, int sampleRate, int numChannels, faac_real initial_quality)
{
    if (bitRate) {
        abr->desbits = numChannels * (int)((bitRate * FRAME_LEN) / sampleRate);
        abr->bit_reservoir_max = 6 * abr->desbits;
        abr->bit_reservoir     = abr->bit_reservoir_max / 2;
        abr->quality_base      = initial_quality;
    }
}

void AbrUpdate(abr_t *abr, int frameBytes, faac_real *quality, int maxqual)
{
    const int desbits     = abr->desbits;
    const int actual_bits = frameBytes * 8;

    /* ── Slow loop: single-pole IIR on the bitrate ratio ─────────────────
     *
     * quality_base *= EMA(desbits / actual_bits, α=1/16)
     *
     * At α = 1/16, the time constant is 16 frames (~0.35 s at 44.1 kHz,
     * ~1 s at 16 kHz).  The per-frame ratio is hard-clamped to [0.5, 2.0]
     * so a single outlier frame (silence, flush) can't collapse quality.
     *
     * Because quality_base is updated multiplicatively via a ratio that
     * averages to 1.0 at the correct bitrate, it converges without
     * overshoot and without the oscillation that a direct P-controller
     * introduces.
     */
    faac_real ratio;

    if (actual_bits > 0) {
        ratio = (faac_real)desbits / (faac_real)actual_bits;
    } else {
        ratio = 2.0f; /* Treat zero-bit frames as maximum undershoot */
    }

    if (ratio > 2.0f) ratio = 2.0f;
    if (ratio < 0.5f) ratio = 0.5f;
    abr->quality_base *= (15.0f / 16.0f) + (1.0f / 16.0f) * ratio;

    /* ── Reservoir update ────────────────────────────────────────────────
     *
     * Accumulate the per-frame bit surplus.  The floor at 0 enforces
     * causality: you cannot spend bits that haven't been saved yet.
     * The ceiling prevents unbounded wind-up over long silent passages.
     */
    abr->bit_reservoir += desbits - actual_bits;
    if (abr->bit_reservoir < 0)
        abr->bit_reservoir = 0;
    if (abr->bit_reservoir > abr->bit_reservoir_max)
        abr->bit_reservoir = abr->bit_reservoir_max;

    /* ── Reservoir modulation ─────────────────────────────────────────────
     *
     * Map reservoir fullness [0, max] → quality multiplier [0.8, 1.2].
     * Neutral at half-full (fill = 0.5 → multiplier = 1.0).
     *
     * This is the VBR-like component: simple/tonal frames underspend
     * relative to quality_base, fill the reservoir, and boost the
     * multiplier for the complex/transient frame that follows.  The slow
     * loop does not need to react to these short-term swings at all.
     */
    faac_real fill    = (faac_real)abr->bit_reservoir
                      / (faac_real)abr->bit_reservoir_max;
    faac_real res_mul = 0.8f + 0.4f * fill;   /* [0.8 … 1.2] */

    /* ── Compose and clamp ───────────────────────────────────────────── */
    *quality = abr->quality_base * res_mul;

    if (abr->quality_base > (faac_real)maxqual) abr->quality_base = (faac_real)maxqual;
    if (abr->quality_base < 10.0f)      abr->quality_base = 10.0f;
    if (*quality > (faac_real)maxqual)
        *quality = (faac_real)maxqual;
    if (*quality < 10.0f)
        *quality = 10.0f;
}
