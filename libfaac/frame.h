/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2001 Menno Bakker
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef FRAME_H
#define FRAME_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <faac.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "coder.h"
#include "channels.h"
#include "blockswitch.h"
#include "fft.h"
#include "quantize.h"

#include <faaccfg.h>

struct faacEncStruct {
    /* number of channels in AAC file */
    unsigned int numChannels;

    /* samplerate of AAC file */
    unsigned long sampleRate;
    unsigned int sampleRateIdx;

    unsigned int usedBytes;

    /* frame number */
    unsigned int frameNum;
    unsigned int flushFrame;

    /* Scalefactorband data */
    SR_INFO *srInfo;

    /* sample buffers of current next and next next frame*/
    faac_real *sampleBuff[MAX_CHANNELS];
    faac_real *next3SampleBuff[MAX_CHANNELS];

    /* Filterbank buffers */
    faac_real *sin_window_long;
    faac_real *sin_window_short;
    faac_real *kbd_window_long;
    faac_real *kbd_window_short;
    faac_real *freqBuff[MAX_CHANNELS];
    faac_real *overlapBuff[MAX_CHANNELS];

    faac_real *msSpectrum[MAX_CHANNELS];

    /* Channel and Coder data for all channels */
    CoderInfo coderInfo[MAX_CHANNELS];
    ChannelInfo channelInfo[MAX_CHANNELS];

    /* Psychoacoustics data */
    PsyInfo psyInfo[MAX_CHANNELS];
    GlobalPsyInfo gpsyInfo;

    /* Configuration data */
    faacEncConfiguration config;

    struct psymodel_s *psymodel;

    /* quantizer specific config */
    AACQuantCfg aacquantCfg;

    /* FFT Tables */
    FFT_Tables	fft_tables;

    /* Temporary buffers for FilterBank, MDCT and IMDCT to avoid reallocations */
    faac_real *transf_buf;
    faac_real *overlap_buf;
    faac_real *mdct_xi;
    faac_real *mdct_xr;

    /* Twiddle factors for MDCT/IMDCT */
    faac_real *mdct_twiddles_long;
    faac_real *mdct_twiddles_short;

    /* Temporary buffer for TNS */
    faac_real *tns_temp;

    ALIGN16_BEG faac_real pow10_sfstep[256] ALIGN16_END;
    int pow10_sfstep_init;
    ALIGN16_BEG int xitab[FRAME_LEN] ALIGN16_END;
};

typedef struct faacEncStruct faacEncStruct;

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FRAME_H */
