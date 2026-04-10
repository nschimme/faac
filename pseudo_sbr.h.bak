#ifndef PSEUDO_SBR_H
#define PSEUDO_SBR_H
#include "faac_real.h"
#include "coder.h"
#ifdef __cplusplus
extern "C" {
#endif
void PseudoSBRApply(faac_real *freqBuff, int sbr_start_sfb, const int *cb_widths, unsigned long bitRatePerChannel);
int PseudoSBRShouldEnable(unsigned long bitRatePerChannel, unsigned int sampleRate);
unsigned int PseudoSBRTargetBW(unsigned long bitRatePerChannel, unsigned int sampleRate);
#ifdef __cplusplus
}
#endif
#endif
