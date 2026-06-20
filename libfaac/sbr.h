#ifndef SBR_H
#define SBR_H
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "faac_real.h"
#include "coder.h"
#include "fft.h"
#define SBR_QMF_OVL_LEN 64
#define SBR_QMF_OVL_LEN_64 640
#define SBR_MAX_BANDS 64
#define SBR_MAX_ENVELOPES 2
#define SBR_MAX_NOISE_BANDS 5
#define SBR_EXT_TYPE_SBR 0xd
typedef struct SBRInfo {
    int sbrPresent, headerSent, frameCount, numChannels, sampleRate, coreSampleRate, kx, k2, dk, numEnvelopes;
    float transientThresh;
    int numBands, bandEdges[SBR_MAX_BANDS + 1], numNoiseBands, noiseBandEdges[SBR_MAX_NOISE_BANDS + 1];
    int bs_amp_res, bs_freq_res, eff_amp_res, bs_start_freq, bs_stop_freq, bs_xover_band, bs_alter_scale;
    faac_real qmfOvl[MAX_CHANNELS][SBR_QMF_OVL_LEN];
    faac_real qmfOvl64[MAX_CHANNELS][SBR_QMF_OVL_LEN_64];
    int envData[MAX_CHANNELS][SBR_MAX_ENVELOPES][SBR_MAX_BANDS], noiseData[MAX_CHANNELS][SBR_MAX_NOISE_BANDS], invfMode[MAX_CHANNELS];
    faac_real twidCos[128], twidSin[128];
    FFT_Tables fftTables;
} SBRInfo;
SBRInfo *SBRInit(int channels, int sampleRate, int coreSampleRate, unsigned long bitRate);
void SBREnd(SBRInfo *sbr);
void SBRAnalysis(SBRInfo *sbr, faac_real *timeDomain[MAX_CHANNELS], int numChannels, int numSamples);
#include "bitstream.h"
int SBRWriteBitstream(SBRInfo *sbr, BitStream *bs, int id_aac, int writeFlag);
void qmf_analysis_64_slot_energy_test(const SBRInfo *sbr, const faac_real * restrict slot, faac_real * restrict ovl, faac_real * restrict energy, int kx, int k2);
void qmf_analysis_slot_complex(const SBRInfo *sbr, const faac_real *slot, faac_real *ovl, faac_real *W_re, faac_real *W_im);
#endif
