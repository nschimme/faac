/*
 * FAAR - Freeware Advanced Audio Resampler
 * Copyright (C) 2026 Nils Schimmelmann
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 */
#ifndef FAAR_DFT_COEFFS_H
#define FAAR_DFT_COEFFS_H

#include <stddef.h>

/* Design an odd-length, unity-DC Kaiser-windowed low-pass FIR. cutoff is in
 * cycles/sample (0 < cutoff < 0.5). Returns 0 or -1 for invalid arguments. */
int faar_dft_design_lowpass(double *taps, size_t tap_count,
                            double cutoff, double beta);

#endif
