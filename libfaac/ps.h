#ifndef FAAC_PS_H
#define FAAC_PS_H
#include <stdbool.h>
#define PS_BANDS 20
#define PS_PHASE_BANDS 11
struct SBRInfo;
struct SignalAnalysis;
struct SbrFrameData;
struct BitStream;
typedef struct PsHybrid {
    float history[2][3][6][2], coef[8][7][2];
    int initialized;
} PsHybrid;
void PsHybridAnalyze(PsHybrid *, const float [2][3][44][2],
                     float [2][4][64], float [4][64], float [4][64], int);
typedef struct PsCarrier {
    float history[2][256], overlap[256], window[512];
    int initialized;
    double smoothL, smoothR, smoothCross;
} PsCarrier;
void PsSpectralDownmix(PsCarrier *, float *, float *, float *, int);
void PsAnalyze(struct SBRInfo *, struct SignalAnalysis *, struct SbrFrameData *);
void PsDownmix(float *, const float *, int);
int PsWrite(const struct SbrFrameData *, struct BitStream *, bool);
#endif
