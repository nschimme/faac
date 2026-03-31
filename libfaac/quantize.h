#ifndef QUANTIZE_H
#define QUANTIZE_H
#include "coder.h"
#ifdef __cplusplus
extern "C" {
#endif
#define MAGIC_NUMBER 0.4054
#define SF_OFFSET 100
#define MINQUAL 1
#define MAXQUAL 10000
#define MAXQUALADTS 10000
#define DEFQUAL 100
typedef struct { int pnslevel; int quality; int max_cbs; int max_cbl; int max_l; int target_bits; } AACQuantCfg;
void QuantizeInit(void);
void CalcBW(unsigned *bw, int rate, SR_INFO *sr, AACQuantCfg *cfg);
void BlocGroup(faac_real *xr, CoderInfo *ci, AACQuantCfg *cfg);
int BlocQuant(CoderInfo *ci, faac_real *xr, AACQuantCfg *cfg);
void BlocStat(void);
#ifdef __cplusplus
}
#endif
#endif
