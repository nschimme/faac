/****************************************************************************
    Quantizer core functions
****************************************************************************/

#ifndef QUANTIZE_H
#define QUANTIZE_H

#include "coder.h"
#include "faac_real.h"

typedef struct
{
    faac_real quality;
    int max_cbl;
    int max_cbs;
    int max_l;
    int pnslevel;
} AACQuantCfg;

#ifdef FAAC_PRECISION_SINGLE
#define MAGIC_NUMBER 0.4054f
#else
#define MAGIC_NUMBER 0.4054
#endif

enum {
    DEFQUAL = 100,
    MAXQUAL = 5000,
    MAXQUALADTS = MAXQUAL,
    MINQUAL = 10,
    SF_OFFSET = 100,
    MAX_HUFF_ESC_VAL = 8191,
    SF_MIN = 10,
    SF_DELTA = 60,
    PNS_SF_OFFSET = (SF_OFFSET - SF_MIN),
};

static inline int ClampSfDiff(int diff)
{
    if (diff > SF_DELTA) return SF_DELTA;
    if (diff < -SF_DELTA) return -SF_DELTA;
    return diff;
}

int BlocQuant(CoderInfo *coderInfo, faac_real *xr, AACQuantCfg *aacquantCfg);
void CalcBW(unsigned *bw, int rate, SR_INFO *sr, AACQuantCfg *aacquantCfg);
void BlocGroup(faac_real *xr, CoderInfo *coderInfo, AACQuantCfg *aacquantCfg);
void BlocStat(void);
void QuantizeInit(void);

#endif
