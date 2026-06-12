/*
 * FAAC - Freeware Advanced Audio Coder
 *
 * HE-AAC v1 Spectral Band Replication (SBR) encoder
 * ISO/IEC 14496-3:2009 §4.6.18
 */

#ifndef SBR_H
#define SBR_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "faac_real.h"
#include "coder.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SBR_NUM_TIME_SLOTS   32
#define SBR_QMF_BANDS        32
#define SBR_QMF_FILTER_LEN   64
#define SBR_QMF_OVL_LEN      64
#define SBR_QMF_BANDS_64     64
#define SBR_QMF_OVL_LEN_64   640
#define SBR_MAX_BANDS        64
#define SBR_MAX_ENVELOPES     2
#define SBR_MAX_NOISE_BANDS   5
#define SBR_HEADER_PERIOD    30

#define SBR_FRAME_CLASS_FIXFIX  0

typedef struct SBRInfo {
    int sbrPresent;
    int headerSent;
    int frameCount;
    int numChannels;
    int sampleRate;
    int coreSampleRate;

    int kx;
    int k2;
    int dk;                /* Frequency table resolution (1 or 2) */
    int numEnvelopes;      /* envelopes in the CURRENT frame (1 or 2), set by
                              the transient detector in SBRAnalysis */
    float transientThresh; /* slot peak/mean energy ratio for 2-env frames */
    int numBands;
    int bandEdges[SBR_MAX_BANDS + 1];
    int numNoiseBands;
    int noiseBandEdges[SBR_MAX_NOISE_BANDS + 1];

    int bs_amp_res;
    int bs_freq_res;       /* envelope frequency resolution: 1 = HIGH (f_master) */
    int eff_amp_res;       /* amp_res the decoder actually uses: forced to 0 for
                              FIXFIX frames with a single envelope (ISO 14496-3,
                              cf. FAAD2 sbr_huff.c) */
    int bs_start_freq;
    int bs_stop_freq;
    int bs_xover_band;
    int bs_alter_scale;

    faac_real qmfOvl[MAX_CHANNELS][SBR_QMF_OVL_LEN];
    faac_real qmfOvl64[MAX_CHANNELS][SBR_QMF_OVL_LEN_64];
    int envData  [MAX_CHANNELS][SBR_MAX_ENVELOPES][SBR_MAX_BANDS];
    int noiseData[MAX_CHANNELS][SBR_MAX_NOISE_BANDS];
    int invfMode [MAX_CHANNELS];   /* bs_invf_mode 0..3, from spectral flatness */

    faac_real cos_table[SBR_QMF_BANDS][SBR_QMF_FILTER_LEN];
    faac_real sin_table[SBR_QMF_BANDS][SBR_QMF_FILTER_LEN];
    faac_real cos_table64T[128][SBR_QMF_BANDS_64];
    faac_real sin_table64T[128][SBR_QMF_BANDS_64];
    float cos_table64F[128][SBR_QMF_BANDS_64];
    float sin_table64F[128][SBR_QMF_BANDS_64];

    void (*qmf_64_mod)(const struct SBRInfo *sbr, const faac_real * restrict proto,
                       const faac_real * restrict ovl, faac_real * restrict re,
                       faac_real * restrict im);
} SBRInfo;

SBRInfo *SBRInit(int channels, int sampleRate, int coreSampleRate, unsigned long bitRate);
void SBREnd(SBRInfo *sbr);
void SBRAnalysis(SBRInfo *sbr, faac_real *timeDomain[MAX_CHANNELS], int numChannels, int numSamples);
#include "bitstream.h"
int SBRWriteBitstream(SBRInfo *sbr, BitStream *bs, int id_aac, int writeFlag);

#ifdef __cplusplus
}
#endif

#endif
