/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2026 Jules (AI Assistant)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "faac_real.h"
#include "quantize.h"
#include "mxu2_asm.h"

#ifndef FAAC_PRECISION_SINGLE
#error MXU2 implementation only supports single precision floats
#endif

void quantize_mxu2(const faac_real * __restrict xr, int * __restrict xi, int n, faac_real sfacfix)
{
    int cnt = 0;
    const float magic_val = (float)MAGIC_NUMBER;

    if (n >= 4) {
        __asm__ __volatile__ (
            ".set push\n\t"
            ".set noreorder\n\t"

            // Load constants into GPRs
            "lw $t4, %2\n\t"        // sfacfix
            "li $t5, 0x7FFFFFFF\n\t" // abs_mask
            "lw $t6, %3\n\t"        // magic
            "move $t7, $zero\n\t"   // zero

            // Fill MXU2 registers with replicated constants
            MXU2_MFCPUW(1, 12)      // vr1 = sfacfix (using $t4)
            MXU2_MFCPUW(2, 13)      // vr2 = abs_mask (using $t5)
            MXU2_MFCPUW(3, 14)      // vr3 = magic (using $t6)
            MXU2_MFCPUW(4, 15)      // vr4 = zero (using $t7)

            "move $t0, %0\n\t"      // xr pointer
            "move $t1, %1\n\t"      // xi pointer
            "move $t2, %4\n\t"      // loop count (n/4)

            "1:\n\t"
            // Load 4 floats
            MXU2_LU1QX(5, 0, 8)     // vr5 = *xr (using $t0, index $zero)

            // sign_mask = x < 0
            MXU2_FCLTW(6, 5, 4)     // vr6 = vr5 < vr4 (zero)

            // x = x & abs_mask
            MXU2_ANDV(5, 5, 2)      // vr5 = vr5 & vr2 (abs_mask)

            // x = x * sfac
            MXU2_FMULW(5, 5, 1)     // vr5 = vr5 * vr1 (sfac)

            // x = x * sqrt(x)
            MXU2_FSQRTW(7, 5)       // vr7 = sqrt(vr5)
            MXU2_FMULW(5, 5, 7)     // vr5 = vr5 * vr7

            // x = sqrt(x)
            MXU2_FSQRTW(5, 5)       // vr5 = sqrt(vr5)

            // x = x + magic
            MXU2_FADDW(5, 5, 3)     // vr5 = vr5 + vr3 (magic)

            // Convert to int (truncate)
            MXU2_VTRUNCSWS(7, 5)    // vr7 = (int)vr5

            // Apply sign: (val ^ mask) - mask
            MXU2_XORV(7, 7, 6)      // vr7 = vr7 ^ vr6
            MXU2_SUBW(7, 7, 6)      // vr7 = vr7 - vr6

            // Store 4 ints
            MXU2_SU1QX(7, 0, 9)     // *xi = vr7 (using $t1, index $zero)

            // Increment pointers
            "addiu $t0, $t0, 16\n\t"
            "addiu $t1, $t1, 16\n\t"
            "addiu $t2, $t2, -1\n\t"
            "bnez $t2, 1b\n\t"
            "nop\n\t"

            ".set pop\n\t"
            :
            : "r"(xr), "r"(xi), "m"(sfacfix), "m"(magic_val), "r"(n >> 2)
            : "$t0", "$t1", "$t2", "$t4", "$t5", "$t6", "$t7", "memory"
        );
        cnt = (n >> 2) << 2;
    }

    // Scalar remainder
    for (; cnt < n; cnt++)
    {
        faac_real val = xr[cnt];
        faac_real tmp = FAAC_FABS(val);

        tmp *= sfacfix;
        tmp = FAAC_SQRT(tmp * FAAC_SQRT(tmp));

        int q = (int)(tmp + (faac_real)MAGIC_NUMBER);
        xi[cnt] = (val < 0) ? -q : q;
    }
}
