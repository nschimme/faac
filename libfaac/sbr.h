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
#include "fft.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SBR_QMF_BANDS_64     64
#define SBR_QMF_OVL_LEN_64   576
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
    int sampleRate;        /* full output rate; the core runs at sampleRate/2 */

    int kx;
    int k2;
    int dk;                /* Frequency table resolution (1 or 2) */
    int numEnvelopes;      /* envelopes in the CURRENT frame (1 or 2), set by
                              the transient detector in SBRAnalysis */
    faac_real transientThresh; /* slot peak/mean energy ratio for 2-env frames */
    int numBands;
    int bandEdges[SBR_MAX_BANDS + 1];
    int numNoiseBands;

    int bs_amp_res;
    int bs_freq_res;       /* envelope frequency resolution: 1 = HIGH (f_master) */
    int eff_amp_res;       /* amp_res the decoder actually uses: forced to 0 for
                              FIXFIX frames with a single envelope (ISO 14496-3,
                              cf. FAAD2 sbr_huff.c) */
    int bs_start_freq;
    int bs_stop_freq;
    int bs_xover_band;
    int bs_alter_scale;

    faac_real qmfOvl64[MAX_CHANNELS][SBR_QMF_OVL_LEN_64];
    int envData  [MAX_CHANNELS][SBR_MAX_ENVELOPES][SBR_MAX_BANDS];
    int noiseData[MAX_CHANNELS][SBR_MAX_NOISE_BANDS];
    int invfMode [MAX_CHANNELS];   /* bs_invf_mode; currently fixed at 3 (strongest) */

    /* 64-band analysis via a single 64-point complex FFT: twidCos/Sin pre-rotate
     * the even/odd-packed window output; oddCos/Sin recombine the two halves. */
    faac_real twidCos[64];
    faac_real twidSin[64];
    faac_real oddCos[64];
    faac_real oddSin[64];
    FFT_Tables fftTables;
} SBRInfo;

SBRInfo *SBRInit(int channels, int sampleRate, unsigned long bitRate);
void SBREnd(SBRInfo *sbr);
void SBRAnalysis(SBRInfo *sbr, faac_real *timeDomain[MAX_CHANNELS], int numChannels, int numSamples, int fast_mode);
#include "bitstream.h"
int SBRWriteBitstream(SBRInfo *sbr, BitStream *bs, int id_aac, int writeFlag);

#ifdef __cplusplus
}
#endif

#endif
