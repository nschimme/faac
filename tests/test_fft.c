/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2026 Nils Schimmelmann
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <assert.h>
#include <math.h>
#include "../libfaac/fft.c"

void test_fft() {
    FFT_Tables tables;
    fft_initialize(&tables);

    faac_real xr[8];
    faac_real xi[8];
    int i;

    /* Discrete unit impulse at origin */
    for (i = 0; i < 8; i++) {
        xr[i] = (i == 0) ? 1.0 : 0.0;
        xi[i] = 0.0;
    }

    fft(&tables, xr, xi, 3);

    /* FFT of unit impulse is a constant signal (magnitude 1.0) */
    for (i = 0; i < 8; i++) {
        assert(fabs(xr[i] - 1.0) < 1e-6);
        assert(fabs(xi[i] - 0.0) < 1e-6);
    }

    fft_terminate(&tables);
}

void test_rfft() {
    FFT_Tables tables;
    fft_initialize(&tables);

    faac_real x[8];
    int i;

    for (i = 0; i < 8; i++) {
        x[i] = (i == 0) ? 1.0 : 0.0;
    }

    rfft(&tables, x, 3);

    /* RFFT output mapping: [Re(0...N/2-1), Im(0...N/2-1)] */
    for (i = 0; i < 4; i++) {
        assert(fabs(x[i] - 1.0) < 1e-6);
    }
    for (i = 4; i < 8; i++) {
        assert(fabs(x[i] - 0.0) < 1e-6);
    }

    fft_terminate(&tables);
}

int main() {
    test_fft();
    test_rfft();
    printf("test_fft passed\n");
    return 0;
}
