#ifndef PSY_TABLES_H
#define PSY_TABLES_H

#include "faac_real.h"
#include "coder.h"

#define ATH_TABLE_SIZE 90
#define NSFB_LONG_H 64
#define NSFB_SHORT_H 16

typedef struct {
    faac_real freq;
    faac_real db;
} ATHTableEntry;

typedef struct {
    faac_real ath_long[NSFB_LONG_H];
    faac_real ath_short[NSFB_SHORT_H];
    faac_real spread_matrix_long[NSFB_LONG_H * NSFB_LONG_H];
    faac_real spread_matrix_short[NSFB_SHORT_H * NSFB_SHORT_H];
    faac_real bark_long[NSFB_LONG_H];
    faac_real bark_short[NSFB_SHORT_H];
} FaacPsyContext;

extern const ATHTableEntry iso226_ath_table[ATH_TABLE_SIZE];

void FaacPsyInitContext(FaacPsyContext *ctx, int sample_rate,
                        int *cb_width_long, int num_cb_long,
                        int *cb_width_short, int num_cb_short);

#endif
