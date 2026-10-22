#ifndef FAAC_PS_H
#define FAAC_PS_H
#include <stdbool.h>
#define PS_BANDS 20
#define PS_PHASE_BANDS 11
#define PS_LOW_BANDS 10
#define PS_LOW_PHASE_BANDS 5
#define PS_LOW_RATE_MAX_BITRATE 16000 /* total bits/s, explicit bitrate only */
struct SBRInfo;
struct SignalAnalysis;
struct SbrFrameData;
struct BitStream;
/* Last transmitted envelope. Two slots in SBRInfo keep the preceding
 * access unit stable while rate control sizes/retries the current one. */
typedef struct PsRef {
    int iid[PS_BANDS], icc[PS_BANDS], ipd[PS_PHASE_BANDS], opd[PS_PHASE_BANDS];
    int valid, band_mode;
} PsRef;
typedef struct PsHybrid {
    float history[2][3][6][2], coef[8][7][2];
    int initialized;
} PsHybrid;
void PsHybridAnalyze(PsHybrid *, const float [2][3][44][2],
                     float [2][4][64], float [4][64], float [4][64], int);
typedef struct PsCarrier {
    float history[2][256], overlap[256], window[512];
    float work[1024], leftSpectrum[1024], rightSpectrum[1024];
    int initialized;
    double smoothL, smoothR, smoothCross;
} PsCarrier;
void PsSpectralDownmix(PsCarrier *, float *, float *, float *, int);
void PsAnalyze(struct SBRInfo *, struct SignalAnalysis *, struct SbrFrameData *);
void PsDownmix(float *, const float *, int);
/* ref is the preceding transmitted envelope in the same band mode, or NULL.
 * Sizing/writing never modifies it. */
int PsWrite(const struct SbrFrameData *, struct BitStream *, bool, const PsRef *);
#endif
