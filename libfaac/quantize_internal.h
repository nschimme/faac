#ifndef QUANTIZE_INTERNAL_H
#define QUANTIZE_INTERNAL_H

#include "quantize.h"

void bmask_test(CoderInfo * __restrict coderInfo, faac_real * __restrict xr0, faac_real * __restrict bandqual,
                faac_real * __restrict bandenrg, faac_real * __restrict bandmaxe, int gnum, faac_real quality,
                int heMode);

#endif
