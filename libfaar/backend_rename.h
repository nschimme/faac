/* Private symbol namespace for a precision-specific FAAR engine. */
#ifndef FAAR_BACKEND_RENAME_H
#define FAAR_BACKEND_RENAME_H

#if defined(FAAR_BACKEND_FLOAT)
# define FAAR_BACKEND_SYMBOL(name) faar_float_##name
#elif defined(FAAR_BACKEND_DOUBLE)
# define FAAR_BACKEND_SYMBOL(name) faar_double_##name
#endif

#ifdef FAAR_BACKEND_SYMBOL
# define faar_resampler_open FAAR_BACKEND_SYMBOL(resampler_open)
# define faar_resampler_prepare FAAR_BACKEND_SYMBOL(resampler_prepare)
# define faar_resampler_close FAAR_BACKEND_SYMBOL(resampler_close)
# define faar_resampler_get_info FAAR_BACKEND_SYMBOL(resampler_get_info)
# define faar_resampler_process FAAR_BACKEND_SYMBOL(resampler_process)
# define faar_resampler_flush FAAR_BACKEND_SYMBOL(resampler_flush)

# define faar_dft_fir_init FAAR_BACKEND_SYMBOL(dft_fir_init)
# define faar_dft_fir_init_expanded FAAR_BACKEND_SYMBOL(dft_fir_init_expanded)
# define faar_dft_fir_init_rate FAAR_BACKEND_SYMBOL(dft_fir_init_rate)
# define faar_dft_fir_init_paired FAAR_BACKEND_SYMBOL(dft_fir_init_paired)
# define faar_dft_fir_init_shared FAAR_BACKEND_SYMBOL(dft_fir_init_shared)
# define faar_dft_fir_init_halfband FAAR_BACKEND_SYMBOL(dft_fir_init_halfband)
# define faar_dft_fir_init_compensated FAAR_BACKEND_SYMBOL(dft_fir_init_compensated)
# define faar_dft_fir_clear FAAR_BACKEND_SYMBOL(dft_fir_clear)
# define faar_dft_fir_prepare FAAR_BACKEND_SYMBOL(dft_fir_prepare)
# define faar_dft_fir_process FAAR_BACKEND_SYMBOL(dft_fir_process)
# define faar_dft_fir_reset FAAR_BACKEND_SYMBOL(dft_fir_reset)
# define faar_dft_fir_stream FAAR_BACKEND_SYMBOL(dft_fir_stream)
# define faar_dft_fir_flush FAAR_BACKEND_SYMBOL(dft_fir_flush)

# define faar_dft_prefilter_create FAAR_BACKEND_SYMBOL(dft_prefilter_create)
# define faar_dft_prefilter_create_expanded FAAR_BACKEND_SYMBOL(dft_prefilter_create_expanded)
# define faar_dft_prefilter_create_rate FAAR_BACKEND_SYMBOL(dft_prefilter_create_rate)
# define faar_dft_prefilter_create_stage FAAR_BACKEND_SYMBOL(dft_prefilter_create_stage)
# define faar_dft_prefilter_destroy FAAR_BACKEND_SYMBOL(dft_prefilter_destroy)
# define faar_dft_prefilter_prepare FAAR_BACKEND_SYMBOL(dft_prefilter_prepare)
# define faar_dft_prefilter_set_prepared FAAR_BACKEND_SYMBOL(dft_prefilter_set_prepared)
# define faar_dft_prefilter_set_boundary FAAR_BACKEND_SYMBOL(dft_prefilter_set_boundary)
# define faar_dft_prefilter_process FAAR_BACKEND_SYMBOL(dft_prefilter_process)
# define faar_dft_prefilter_flush FAAR_BACKEND_SYMBOL(dft_prefilter_flush)
# define faar_dft_prefilter_reset FAAR_BACKEND_SYMBOL(dft_prefilter_reset)

/* Backend entry points are private implementation symbols, not ABI exports. */
# ifndef FAARAPI
#  define FAARAPI
# endif
#endif

#endif
