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
#include "quantize_mxu.h"

#ifndef FAAC_PRECISION_SINGLE
#error MXU2 implementation only supports single precision floats
#endif

void quantize_mxu2(const faac_real * __restrict xr, int * __restrict xi, int n, faac_real sfacfix)
{
    int cnt = 0;
    const float magic_val = (float)MAGIC_NUMBER;

    if (n >= 4) {
        /*
         * Load constants into MXU2 registers once.
         */
        __asm__ __volatile__ (
            "lw $t4, %[sfac]\n\t"
            "li $t5, 0x7FFFFFFF\n\t"
            "lw $t6, %[magic]\n\t"
            "move $t7, $zero\n\t"
            MXU2_MFCPUW(1, 12)      // vr1 = sfacfix ($t4=12)
            MXU2_MFCPUW(2, 13)      // vr2 = abs_mask ($t5=13)
            MXU2_MFCPUW(3, 14)      // vr3 = magic ($t6=14)
            MXU2_MFCPUW(4, 15)      // vr4 = zero ($t7=15)
            :
            : [sfac] "m"(sfacfix), [magic] "m"(magic_val)
            : "$t4", "$t5", "$t6", "$t7"
        );

        /* Process 4 elements per iteration */
        for (; cnt <= n - 4; cnt += 4) {
            __asm__ __volatile__ (
                "move $t0, %[ptr_xr]\n\t"
                "move $t1, %[ptr_xi]\n\t"

                // Load 4 floats
                MXU2_LU1QX(5, 0, 8)     // vr5 = *xr ($t0=8)

                // sign_mask = x < 0
                MXU2_FCLTW(6, 5, 4)     // vr6 = vr5 < vr4 (zero)

                // x = x & abs_mask
                MXU2_ANDV(5, 5, 2)      // vr5 = abs(vr5)

                // x = x * sfac
                MXU2_FMULW(5, 5, 1)     // vr5 *= sfac

                // x = x^0.75: x = sqrt(x * sqrt(x))
                MXU2_FSQRTW(7, 5)       // vr7 = sqrt(vr5)
                MXU2_FMULW(5, 5, 7)     // vr5 *= vr7
                MXU2_FSQRTW(5, 5)       // vr5 = sqrt(vr5)

                // x = x + magic
                MXU2_FADDW(5, 5, 3)     // vr5 += magic

                // Convert to int (truncate)
                MXU2_VTRUNCSWS(7, 5)    // vr7 = (int)vr5

                // Apply sign: (val ^ mask) - mask
                MXU2_XORV(7, 7, 6)      // apply sign
                MXU2_SUBW(7, 7, 6)

                // Store 4 ints
                MXU2_SU1QX(7, 0, 9)     // *xi = vr7 ($t1=9)
                :
                : [ptr_xr] "r"(&xr[cnt]), [ptr_xi] "r"(&xi[cnt])
                : "$t0", "$t1", "memory"
            );
        }
    }

    /* Scalar remainder loop */
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
