/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2026 Jules (AI Assistant)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#ifndef QUANTIZE_MXU_H
#define QUANTIZE_MXU_H

#include "faac_real.h"

#if defined(__mips__)
void QuantizeInitMXU(void);
void quantize_mxu1(const faac_real * __restrict xr, int * __restrict xi, int n, faac_real sfacfix);
void quantize_mxu2(const faac_real * __restrict xr, int * __restrict xi, int n, faac_real sfacfix);

int check_mxu1_support(void);
int check_mxu2_support(void);
unsigned int get_mips_prid(void);
#endif

#endif /* QUANTIZE_MXU_H */
