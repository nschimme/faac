#ifndef BUILTIN_TABLES_H
#define BUILTIN_TABLES_H

#include "faac_real.h"

extern const faac_real pow10_sfstep_table[256];
#ifndef DRM
extern const faac_real fft_costbl_6[32];
extern const faac_real fft_negsintbl_6[32];
extern const unsigned short fft_reorder_6[64];
extern const faac_real fft_costbl_9[256];
extern const faac_real fft_negsintbl_9[256];
extern const unsigned short fft_reorder_9[512];
#endif
extern const faac_real sin_window_long_table[1024];
extern const faac_real sin_window_short_table[128];
extern const faac_real kbd_window_long_table[1024];
extern const faac_real kbd_window_short_table[128];
extern const faac_real mdct_twiddles_long_table[1024];
extern const faac_real mdct_twiddles_short_table[128];
#endif
