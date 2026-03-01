#ifndef QUANTIZE_INTERNAL_H
#define QUANTIZE_INTERNAL_H

#include "faac_real.h"
#include "coder.h"

void quantize_lines(int end, faac_real sfacfix, const faac_real *xr, int *xi);

/* Architecture specific implementations */
void quantize_lines_sse2(int end, faac_real sfacfix, const faac_real *xr, int *xi);
void quantize_lines_avx2(int end, faac_real sfacfix, const faac_real *xr, int *xi);
void quantize_lines_neon(int end, faac_real sfacfix, const faac_real *xr, int *xi);

#endif /* QUANTIZE_INTERNAL_H */
