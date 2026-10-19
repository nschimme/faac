#ifndef FAAR_DFT_COEFFS_H
#define FAAR_DFT_COEFFS_H

#include <stddef.h>

/* Design an odd-length, unity-DC Kaiser-windowed low-pass FIR. cutoff is in
 * cycles/sample (0 < cutoff < 0.5). Returns 0 or -1 for invalid arguments. */
int faar_dft_design_lowpass(double *taps, size_t tap_count,
                            double cutoff, double beta);

#endif
