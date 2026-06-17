/* SBR analysis QMF reference — ported from FFmpeg aacsbr_template.c.
 * Self-contained (libm only); naive O(N^2) IMDCT replaces FFmpeg's fast path. */
#ifndef QMF_ORACLE_H
#define QMF_ORACLE_H

#include "libfaac/faac_real.h"

/* Processes one slot: 32 new input samples -> 32 complex subband outputs.
 * state[320] must be zero-initialised on the first call and is updated
 * in place.  Output is unscaled (scale factor = 1.0). */
void qmf_ref_slot(const faac_real input[32],
                  faac_real state[320],
                  faac_real W_re[32],
                  faac_real W_im[32]);

void qmf_ref_64_slot_energy(const faac_real input[64],
                             faac_real state[640],
                             faac_real energy[64]);

#endif
