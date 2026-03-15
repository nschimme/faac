#ifndef QUANTIZE_H
#define QUANTIZE_H

#include "coder.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SF_OFFSET 100
#define DEFQUAL 100
#define MINQUAL 1
#define MAXQUAL 5000
#define MAXQUALADTS 1000

#define MAGIC_NUMBER 0.4054

typedef struct
{
    faac_real quality;
    int pnslevel;
    int max_cbl;
    int max_cbs;
    int max_l;
} AACQuantCfg;

void QuantizeInit(void);
void CalcBW(unsigned *bw, int rate, SR_INFO *sr, AACQuantCfg *aacquantCfg);
void BlocGroup(faac_real *xr, CoderInfo *coderInfo, AACQuantCfg *cfg);
int BlocQuant(CoderInfo *coderInfo, faac_real *xr, AACQuantCfg *aacquantCfg);
void BlocStat(void);

#ifdef __cplusplus
}
#endif

#endif
