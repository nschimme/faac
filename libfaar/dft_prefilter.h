#ifndef FAAR_DFT_PREFILTER_H
#define FAAR_DFT_PREFILTER_H

#include <stddef.h>
#include <stdint.h>

typedef struct faar_dft_prefilter faar_dft_prefilter;

faar_dft_prefilter *faar_dft_prefilter_create(uint16_t channels,
                                               const double *taps,
                                               size_t tap_count,
                                               size_t fft_size);
faar_dft_prefilter *faar_dft_prefilter_create_expanded(uint16_t channels,
                                                const double *taps,
                                                size_t tap_count,
                                                size_t fft_size,
                                                size_t expansion);
/* fft_size == 0 selects a sparse time-domain half-band (expansion=1,
 * decimation=2, N=4m+1); alternate non-centre taps must be zero. */
faar_dft_prefilter *faar_dft_prefilter_create_rate(uint16_t channels,
                                               const double *taps,
                                               size_t tap_count,
                                               size_t fft_size,
                                               size_t expansion,
                                               size_t decimation);
void faar_dft_prefilter_destroy(faar_dft_prefilter *stage);
int faar_dft_prefilter_set_boundary(faar_dft_prefilter *stage, int hold);
int faar_dft_prefilter_process(faar_dft_prefilter *stage, const float *input,
                               size_t frames, float *output,
                               size_t output_capacity, size_t *produced);
int faar_dft_prefilter_flush(faar_dft_prefilter *stage, float *output,
                             size_t output_capacity, size_t *produced);
void faar_dft_prefilter_reset(faar_dft_prefilter *stage);

#endif
