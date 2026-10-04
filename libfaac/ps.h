#ifndef FAAC_PS_H
#define FAAC_PS_H
#include <stdbool.h>
#define PS_BANDS 10
#define PS_PHASE_BANDS 5
struct SBRInfo;
struct SignalAnalysis;
struct SbrFrameData;
struct BitStream;
void PsAnalyze(struct SBRInfo *, struct SignalAnalysis *, struct SbrFrameData *);
void PsDownmix(float *, const float *, int);
int PsWrite(const struct SbrFrameData *, struct BitStream *, bool);
#endif
