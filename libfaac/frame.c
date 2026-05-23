/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2001 Menno Bakker
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "frame.h"
#include "coder.h"
#include "channels.h"
#include "bitstream.h"
#include "filtbank.h"
#include "quantize.h"
#include "util.h"
#include "tns.h"
#include "stereo.h"
#include "sbr.h"
#include "resample.h"

#if (defined WIN32 || defined _WIN32 || defined WIN64 || defined _WIN64) && !defined(PACKAGE_VERSION)
#include "win32_ver.h"
#endif

#define RC_DEADBAND_THRESHOLD  0.01
#define RC_DAMPING_FACTOR      0.6

static char *libfaacName = PACKAGE_VERSION;
static char *libCopyright =
  "FAAC - Freeware Advanced Audio Coder (http://faac.sourceforge.net/)\n"
  " Copyright (C) 1999,2000,2001  Menno Bakker\n"
  " Copyright (C) 2002,2003,2017  Krzysztof Nikiel\n"
  "This software is based on the ISO MPEG-4 reference source code.\n";

static const psymodellist_t psymodellist[] = {
  {&psymodel2, "knipsycho psychoacoustic"},
  {NULL}
};

static SR_INFO srInfo[12+1];

static unsigned int CalcBandwidth(unsigned long bitRate, unsigned long sampleRate)
{
    const unsigned int nyquist = sampleRate / 2;
    unsigned int bw;

    if (!bitRate) return nyquist;

    if (bitRate <= 16000) {
        bw = 3000 + (bitRate / 32);
    } else if (bitRate <= 32000) {
        bw = 3500 + ((bitRate - 16000) / 4);
    } else if (bitRate <= 64000) {
        bw = 11000 + ((bitRate - 32000) * 15 / 64);
    } else if (bitRate <= 128000) {
        bw = 18500 + ((bitRate - 64000) * 3 / 128);
    } else {
        bw = 20000 + ((bitRate - 128000) / 16);
        if (bw > 20000) bw = 20000;
    }

    return (bw > nyquist) ? nyquist : bw;
}

static void HeAacBuffersFree(faacEncStruct *hEncoder)
{
    int channel;
    for (channel = 0; channel < MAX_CHANNELS; channel++) {
        if (hEncoder->heFullRatePtr[channel]) {
            FreeMemory(hEncoder->heFullRatePtr[channel]);
            hEncoder->heFullRatePtr[channel] = NULL;
        }
        if (hEncoder->heHalfRatePtr[channel]) {
            FreeMemory(hEncoder->heHalfRatePtr[channel]);
            hEncoder->heHalfRatePtr[channel] = NULL;
        }
    }
}

static int HeAacBuffersAlloc(faacEncStruct *hEncoder)
{
    unsigned int i;
    for (i = 0; i < hEncoder->numChannels; i++) {
        if (!hEncoder->heFullRatePtr[i]) {
            hEncoder->heFullRatePtr[i] = (faac_real *)AllocMemory(2 * FRAME_LEN * sizeof(faac_real));
            if (!hEncoder->heFullRatePtr[i]) {
                HeAacBuffersFree(hEncoder);
                return 0;
            }
        }
        if (!hEncoder->heHalfRatePtr[i]) {
            hEncoder->heHalfRatePtr[i] = (faac_real *)AllocMemory(FRAME_LEN * sizeof(faac_real));
            if (!hEncoder->heHalfRatePtr[i]) {
                HeAacBuffersFree(hEncoder);
                return 0;
            }
        }
    }
    return 1;
}

int FAACAPI faacEncGetVersion( char **faac_id_string, char **faac_copyright_string)
{
  if (faac_id_string) *faac_id_string = libfaacName;
  if (faac_copyright_string) *faac_copyright_string = libCopyright;
  return FAAC_CFG_VERSION;
}

int FAACAPI faacEncGetDecoderSpecificInfo(faacEncHandle hpEncoder,unsigned char** ppBuffer,unsigned long* pSizeOfDecoderSpecificInfo)
{
    faacEncStruct* hEncoder = (faacEncStruct*)hpEncoder;
    BitStream* pBitStream = NULL;

    if((hEncoder == NULL) || (ppBuffer == NULL) || (pSizeOfDecoderSpecificInfo == NULL)) return -1;
    if(hEncoder->config.mpegVersion == MPEG2) return -2;

    if (hEncoder->config.aacObjectType == HE_AAC) {
        *pSizeOfDecoderSpecificInfo = 5;
        *ppBuffer = malloc(5);
        if (*ppBuffer == NULL) return -3;
        memset(*ppBuffer, 0, 5);
        pBitStream = OpenBitStream(5, *ppBuffer);
        PutBit(pBitStream, LOW,                       5);
        PutBit(pBitStream, hEncoder->sampleRateIdx,   4);
        PutBit(pBitStream, hEncoder->numChannels,     4);
        PutBit(pBitStream, 0, 1);
        PutBit(pBitStream, 0, 1);
        PutBit(pBitStream, 0, 1);
        PutBit(pBitStream, 0x2b7,                    11);
        PutBit(pBitStream, 5,                         5);
        PutBit(pBitStream, 1,                         1);
        PutBit(pBitStream, hEncoder->fullSampleRateIdx, 4);
        CloseBitStream(pBitStream);
        return 0;
    }

    *pSizeOfDecoderSpecificInfo = 2;
    *ppBuffer = malloc(2);
    if(*ppBuffer != NULL){
        memset(*ppBuffer,0,*pSizeOfDecoderSpecificInfo);
        pBitStream = OpenBitStream(*pSizeOfDecoderSpecificInfo, *ppBuffer);
        PutBit(pBitStream, hEncoder->config.aacObjectType, 5);
        PutBit(pBitStream, hEncoder->sampleRateIdx, 4);
        PutBit(pBitStream, hEncoder->numChannels, 4);
        CloseBitStream(pBitStream);
        return 0;
    } else {
        return -3;
    }
}

faacEncConfigurationPtr FAACAPI faacEncGetCurrentConfiguration(faacEncHandle hpEncoder)
{
    faacEncStruct* hEncoder = (faacEncStruct*)hpEncoder;
    return &(hEncoder->config);
}

int FAACAPI faacEncSetConfiguration(faacEncHandle hpEncoder, faacEncConfigurationPtr config)
{
    faacEncStruct* hEncoder = (faacEncStruct*)hpEncoder;
    int i;
    int maxqual = hEncoder->config.outputFormat ? MAXQUALADTS : MAXQUAL;

    hEncoder->config.jointmode = config->jointmode;
    hEncoder->config.useLfe = config->useLfe;
    hEncoder->config.useTns = config->useTns;
    hEncoder->config.aacObjectType = config->aacObjectType;
    hEncoder->config.mpegVersion = config->mpegVersion;
    hEncoder->config.outputFormat = config->outputFormat;
    hEncoder->config.inputFormat = config->inputFormat;
    hEncoder->config.shortctl = config->shortctl;

    if (hEncoder->config.aacObjectType != LOW &&
        hEncoder->config.aacObjectType != HE_AAC &&
        hEncoder->config.aacObjectType != AAC_AUTO)
        return 0;

    if (!hEncoder->sampleRate || !hEncoder->numChannels) return 0;

    {
        unsigned long fullRate = hEncoder->fullSampleRate ? hEncoder->fullSampleRate : hEncoder->sampleRate;
        if (config->bitRate > (MaxBitrate(fullRate) / hEncoder->numChannels))
            config->bitRate = MaxBitrate(fullRate) / hEncoder->numChannels;
    }

    if (hEncoder->config.aacObjectType == AAC_AUTO) {
        unsigned long rate_per_ch = config->bitRate;
        int rate_ok = (rate_per_ch >= 20000 && rate_per_ch <= 32000);
        int sr_ok = (hEncoder->sampleRate >= 32000);
        hEncoder->config.aacObjectType = (rate_ok && sr_ok) ? HE_AAC : LOW;
        config->aacObjectType = hEncoder->config.aacObjectType;
    }

    if (hEncoder->config.aacObjectType == HE_AAC && hEncoder->fullSampleRate == 0) {
        hEncoder->fullSampleRate    = hEncoder->sampleRate;
        hEncoder->fullSampleRateIdx = hEncoder->sampleRateIdx;
        hEncoder->sampleRate        = hEncoder->sampleRate / 2;
        hEncoder->sampleRateIdx     = GetSRIndex(hEncoder->sampleRate);
        hEncoder->srInfo            = &srInfo[hEncoder->sampleRateIdx];
        hEncoder->config.mpegVersion = MPEG4;
        hEncoder->config.pnslevel    = 0;
    }

    TnsInit(hEncoder);

    if (config->bitRate && !config->bandWidth) {
        config->bandWidth = CalcBandwidth(config->bitRate, hEncoder->sampleRate);
        if (!config->quantqual) {
            config->quantqual = (faac_real)config->bitRate * hEncoder->numChannels / 1280;
            if (config->quantqual > DEFQUAL)
                config->quantqual = (config->quantqual - DEFQUAL) * 3.0 + DEFQUAL;
        }
    }

    if (!config->quantqual) config->quantqual = DEFQUAL;
    hEncoder->config.bitRate = config->bitRate;
    if (!config->bandWidth) config->bandWidth = CalcBandwidth(config->bitRate, hEncoder->sampleRate);
    hEncoder->config.bandWidth = config->bandWidth;

    if (hEncoder->config.bandWidth < 100) hEncoder->config.bandWidth = 100;
    if (hEncoder->config.bandWidth > (hEncoder->sampleRate / 2)) hEncoder->config.bandWidth = hEncoder->sampleRate / 2;

    if (config->quantqual > maxqual) config->quantqual = maxqual;
    if (config->quantqual < MINQUAL) config->quantqual = MINQUAL;
    hEncoder->config.quantqual = config->quantqual;

    if (config->mpegVersion == MPEG2) hEncoder->config.pnslevel = 0;
    if (hEncoder->config.pnslevel < 0) hEncoder->config.pnslevel = 0;
    if (hEncoder->config.pnslevel > 10) hEncoder->config.pnslevel = 10;
    hEncoder->aacquantCfg.pnslevel = hEncoder->config.pnslevel;
    hEncoder->aacquantCfg.quality = hEncoder->config.quantqual;

    hEncoder->psymodel->PsyEnd(&hEncoder->gpsyInfo, hEncoder->psyInfo, hEncoder->numChannels);
    if (config->psymodelidx >= (sizeof(psymodellist) / sizeof(psymodellist[0]) - 1))
		config->psymodelidx = (sizeof(psymodellist) / sizeof(psymodellist[0])) - 2;

    hEncoder->config.psymodelidx = config->psymodelidx;
    hEncoder->psymodel = (psymodel_t *)psymodellist[hEncoder->config.psymodelidx].ptr;
    hEncoder->psymodel->PsyInit(&hEncoder->gpsyInfo, hEncoder->psyInfo, hEncoder->numChannels,
			hEncoder->sampleRate, hEncoder->srInfo->cb_width_long,
			hEncoder->srInfo->num_cb_long, hEncoder->srInfo->cb_width_short,
			hEncoder->srInfo->num_cb_short);

	for( i = 0; i < MAX_CHANNELS; i++ )
		hEncoder->config.channel_map[i] = config->channel_map[i];

    if (hEncoder->config.aacObjectType == HE_AAC) {
        if (!hEncoder->resampler) hEncoder->resampler = ResampleOpen(hEncoder->numChannels);
        if (!hEncoder->sbrInfo) hEncoder->sbrInfo = SBRInit(hEncoder->numChannels, hEncoder->fullSampleRate, hEncoder->sampleRate, hEncoder->config.bitRate * hEncoder->numChannels);
        if (!HeAacBuffersAlloc(hEncoder)) return 0;
        /* HE-AAC (v1): SBR crossover frequency (kx) determines the core AAC-LC bandwidth.
         * kx is expressed in QMF bands [0..63] of the high-rate spectrum.
         * kx_freq = kx * fullSampleRate / 128.
         * Syncing core bandwidth ensures no spectral gaps or overlaps with SBR. */
        unsigned int kx_freq = (unsigned int)((hEncoder->sbrInfo->kx * hEncoder->fullSampleRate) / 128);
        hEncoder->config.bandWidth = kx_freq;
    } else {
        HeAacBuffersFree(hEncoder);
    }

    /* Initialize Bandwidth and Scalefactor Band (SFB) counts for the core encoder. */
    CalcBW(&hEncoder->config.bandWidth, hEncoder->sampleRate, hEncoder->srInfo, &hEncoder->aacquantCfg);

    return 1;
}

faacEncHandle FAACAPI faacEncOpen(unsigned long sampleRate, unsigned int numChannels, unsigned long *inputSamples, unsigned long *maxOutputBytes)
{
    unsigned int channel;
    faacEncStruct* hEncoder;

    if (numChannels > MAX_CHANNELS) return NULL;
    *inputSamples = FRAME_LEN*numChannels;
    *maxOutputBytes = ADTS_FRAMESIZE;

    hEncoder = (faacEncStruct*)AllocMemory(sizeof(faacEncStruct));
    SetMemory(hEncoder, 0, sizeof(faacEncStruct));

    hEncoder->numChannels = numChannels;
    hEncoder->sampleRate = sampleRate;
    hEncoder->sampleRateIdx = GetSRIndex(sampleRate);

    hEncoder->frameNum = 0;
    hEncoder->flushFrame = 0;

    hEncoder->config.version = FAAC_CFG_VERSION;
    hEncoder->config.name = libfaacName;
    hEncoder->config.copyright = libCopyright;
    hEncoder->config.mpegVersion = MPEG4;
    hEncoder->config.aacObjectType = AAC_AUTO;
    hEncoder->config.jointmode = JOINT_IS;
    hEncoder->config.pnslevel = 2;
    hEncoder->config.useLfe = 1;
    hEncoder->config.useTns = 0;
    hEncoder->config.bitRate = 64000;
    hEncoder->config.bandWidth = CalcBandwidth(hEncoder->config.bitRate, sampleRate);
    hEncoder->config.quantqual = 0;
    hEncoder->config.psymodellist = (psymodellist_t *)psymodellist;
    hEncoder->config.psymodelidx = 0;
    hEncoder->psymodel = (psymodel_t *)hEncoder->config.psymodellist[hEncoder->config.psymodelidx].ptr;
    hEncoder->config.shortctl = SHORTCTL_NORMAL;

    HeAacBuffersFree(hEncoder);
	for( channel = 0; channel < MAX_CHANNELS; channel++ ) hEncoder->config.channel_map[channel] = channel;
    hEncoder->config.outputFormat = ADTS_STREAM;
    hEncoder->config.inputFormat = FAAC_INPUT_32BIT;
    hEncoder->srInfo = &srInfo[hEncoder->sampleRateIdx];

    for (channel = 0; channel < numChannels; channel++) {
        hEncoder->coderInfo[channel].prev_window_shape = SINE_WINDOW;
        hEncoder->coderInfo[channel].window_shape = SINE_WINDOW;
        hEncoder->coderInfo[channel].block_type = ONLY_LONG_WINDOW;
        hEncoder->coderInfo[channel].groups.n = 1;
        hEncoder->coderInfo[channel].groups.len[0] = 1;
        hEncoder->sampleBuff[channel] = NULL;
    }

	fft_initialize( &hEncoder->fft_tables );
	hEncoder->psymodel->PsyInit(&hEncoder->gpsyInfo, hEncoder->psyInfo, hEncoder->numChannels, hEncoder->sampleRate, hEncoder->srInfo->cb_width_long, hEncoder->srInfo->num_cb_long, hEncoder->srInfo->cb_width_short, hEncoder->srInfo->num_cb_short);
    FilterBankInit(hEncoder);
    TnsInit(hEncoder);
    QuantizeInit();
    return hEncoder;
}

int FAACAPI faacEncClose(faacEncHandle hpEncoder)
{
    faacEncStruct* hEncoder = (faacEncStruct*)hpEncoder;
    unsigned int channel;

    hEncoder->psymodel->PsyEnd(&hEncoder->gpsyInfo, hEncoder->psyInfo, hEncoder->numChannels);
    FilterBankEnd(hEncoder);
    fft_terminate(&hEncoder->fft_tables);

    for (channel = 0; channel < hEncoder->numChannels; channel++) {
		if (hEncoder->sampleBuff[channel]) FreeMemory(hEncoder->sampleBuff[channel]);
		if (hEncoder->next3SampleBuff[channel]) FreeMemory (hEncoder->next3SampleBuff[channel]);
    }

    if (hEncoder->resampler) {
        ResampleClose(hEncoder->resampler);
        hEncoder->resampler = NULL;
    }
    if (hEncoder->sbrInfo) {
        SBREnd(hEncoder->sbrInfo);
        hEncoder->sbrInfo = NULL;
    }
    HeAacBuffersFree(hEncoder);
    if (hEncoder) FreeMemory(hEncoder);
    BlocStat();
    return 0;
}

#if defined(__GNUC__)
__attribute__((cold, noinline))
#endif
static int doHEAACPreprocess(faacEncStruct *hEncoder, int32_t *inputBuffer, unsigned int samplesInput, faac_real *heHalfRate[MAX_CHANNELS])
{
    unsigned int channel, i;
    unsigned int numChannels = hEncoder->numChannels;
    int full_spch = (int)(samplesInput / numChannels);

    if (full_spch <= 0 || full_spch > 2 * FRAME_LEN) return -1;

    for (channel = 0; channel < numChannels; channel++) {
        faac_real *fullRate = hEncoder->heFullRatePtr[channel];
        switch (hEncoder->config.inputFormat) {
            case FAAC_INPUT_16BIT: {
                short *src = (short *)inputBuffer + hEncoder->config.channel_map[channel];
                for (i = 0; i < (unsigned)full_spch; i++) {
                    fullRate[i] = (faac_real)*src;
                    src += numChannels;
                }
                break;
            }
            case FAAC_INPUT_32BIT: {
                int32_t *src = (int32_t *)inputBuffer + hEncoder->config.channel_map[channel];
                for (i = 0; i < (unsigned)full_spch; i++) {
                    fullRate[i] = (1.0f/256) * (faac_real)*src;
                    src += numChannels;
                }
                break;
            }
            case FAAC_INPUT_FLOAT: {
                float *src = (float *)inputBuffer + hEncoder->config.channel_map[channel];
                for (i = 0; i < (unsigned)full_spch; i++) {
                    fullRate[i] = (faac_real)*src;
                    src += numChannels;
                }
                break;
            }
            default: break;
        }
        heHalfRate[channel] = hEncoder->heHalfRatePtr[channel];
    }

    SBRAnalysis(hEncoder->sbrInfo, hEncoder->heFullRatePtr, numChannels, full_spch);
    Resample2to1(hEncoder->resampler, hEncoder->heFullRatePtr, full_spch, hEncoder->heHalfRatePtr);
    return 0;
}

int FAACAPI faacEncEncode(faacEncHandle hpEncoder, int32_t *inputBuffer, unsigned int samplesInput, unsigned char *outputBuffer, unsigned int bufferSize)
{
    faacEncStruct* hEncoder = (faacEncStruct*)hpEncoder;
    unsigned int channel, i;
    int sb, frameBytes;
    unsigned int offset;
    BitStream *bitStream;

    ChannelInfo *channelInfo = hEncoder->channelInfo;
    CoderInfo *coderInfo = hEncoder->coderInfo;
    unsigned int numChannels = hEncoder->numChannels;
    unsigned int useLfe = hEncoder->config.useLfe;
    unsigned int useTns = hEncoder->config.useTns;
    unsigned int jointmode = hEncoder->config.jointmode;
    unsigned int bandWidth = hEncoder->config.bandWidth;
    unsigned int shortctl = hEncoder->config.shortctl;
    int maxqual = hEncoder->config.outputFormat ? MAXQUALADTS : MAXQUAL;

    hEncoder->frameNum++;
    if (samplesInput == 0) hEncoder->flushFrame++;
    if (hEncoder->flushFrame > 4) return 0;

    GetChannelInfo(channelInfo, numChannels, useLfe);
    faac_real *heHalfRate[MAX_CHANNELS] = {0};

    if (hEncoder->config.aacObjectType == HE_AAC && hEncoder->sbrInfo && hEncoder->resampler && samplesInput > 0)
        if (doHEAACPreprocess(hEncoder, inputBuffer, samplesInput, heHalfRate) < 0) return -1;

    for (channel = 0; channel < numChannels; channel++) {
		faac_real *tmp;
		if (!hEncoder->sampleBuff[channel]) hEncoder->sampleBuff[channel] = (faac_real*)AllocMemory(FRAME_LEN*sizeof(faac_real));
		tmp = hEncoder->sampleBuff[channel];
		hEncoder->sampleBuff[channel]	= hEncoder->next3SampleBuff[channel];
		hEncoder->next3SampleBuff[channel]	= tmp;

        if (samplesInput == 0) {
            for (i = 0; i < FRAME_LEN; i++) hEncoder->next3SampleBuff[channel][i] = 0.0;
        } else if (hEncoder->config.aacObjectType == HE_AAC && heHalfRate[channel]) {
            memcpy(hEncoder->next3SampleBuff[channel], heHalfRate[channel], FRAME_LEN * sizeof(faac_real));
        } else {
			int samples_per_channel = samplesInput/numChannels;
            switch( hEncoder->config.inputFormat ) {
                case FAAC_INPUT_16BIT: {
						short *input_channel = (short*)inputBuffer + hEncoder->config.channel_map[channel];
						for (i = 0; i < samples_per_channel; i++) {
							hEncoder->next3SampleBuff[channel][i] = (faac_real)*input_channel;
							input_channel += numChannels;
						}
					}
                    break;
                case FAAC_INPUT_32BIT: {
						int32_t *input_channel = (int32_t*)inputBuffer + hEncoder->config.channel_map[channel];
						for (i = 0; i < samples_per_channel; i++) {
							hEncoder->next3SampleBuff[channel][i] = (1.0/256) * (faac_real)*input_channel;
							input_channel += numChannels;
						}
					}
                    break;
                case FAAC_INPUT_FLOAT: {
						float *input_channel = (float*)inputBuffer + hEncoder->config.channel_map[channel];
						for (i = 0; i < samples_per_channel; i++) {
							hEncoder->next3SampleBuff[channel][i] = (faac_real)*input_channel;
							input_channel += numChannels;
						}
					}
                    break;
                default: return -1; break;
            }
            for (i = (int)(samplesInput/numChannels); i < FRAME_LEN; i++) hEncoder->next3SampleBuff[channel][i] = 0.0;
		}

		if (channelInfo[channel].type != ELEMENT_LFE) {
			hEncoder->psymodel->PsyBufferUpdate(&hEncoder->fft_tables, &hEncoder->gpsyInfo, &hEncoder->psyInfo[channel], hEncoder->next3SampleBuff[channel], bandWidth, hEncoder->srInfo->cb_width_short, hEncoder->srInfo->num_cb_short);
		}
    }

    if (hEncoder->frameNum <= 3) return 0;

    hEncoder->psymodel->PsyCalculate(channelInfo, &hEncoder->gpsyInfo, hEncoder->psyInfo, hEncoder->srInfo->cb_width_long, hEncoder->srInfo->num_cb_long, hEncoder->srInfo->cb_width_short, hEncoder->srInfo->num_cb_short, numChannels, (faac_real)hEncoder->aacquantCfg.quality / DEFQUAL);

    hEncoder->psymodel->BlockSwitch(coderInfo, hEncoder->psyInfo, numChannels);

    if (shortctl == SHORTCTL_NOSHORT) {
		for (channel = 0; channel < numChannels; channel++) coderInfo[channel].block_type = ONLY_LONG_WINDOW;
    } else if ((hEncoder->frameNum <= 4) || (shortctl == SHORTCTL_NOLONG)) {
		for (channel = 0; channel < numChannels; channel++) coderInfo[channel].block_type = ONLY_SHORT_WINDOW;
    }

    for (channel = 0; channel < numChannels; channel++) {
        FilterBank(hEncoder, &coderInfo[channel], hEncoder->sampleBuff[channel], hEncoder->freqBuff[channel], hEncoder->overlapBuff[channel]);
    }

    for (channel = 0; channel < numChannels; channel++) {
        channelInfo[channel].msInfo.is_present = 0;
        if (coderInfo[channel].block_type == ONLY_SHORT_WINDOW) {
            coderInfo[channel].sfbn = hEncoder->aacquantCfg.max_cbs;
            offset = 0;
            for (sb = 0; sb < coderInfo[channel].sfbn; sb++) {
                coderInfo[channel].sfb_offset[sb] = offset;
                offset += hEncoder->srInfo->cb_width_short[sb];
            }
            coderInfo[channel].sfb_offset[sb] = offset;
            BlocGroup(hEncoder->freqBuff[channel], coderInfo + channel, &hEncoder->aacquantCfg);
        } else {
            coderInfo[channel].sfbn = hEncoder->aacquantCfg.max_cbl;
            coderInfo[channel].groups.n = 1;
            coderInfo[channel].groups.len[0] = 1;
            offset = 0;
            for (sb = 0; sb < coderInfo[channel].sfbn; sb++) {
                coderInfo[channel].sfb_offset[sb] = offset;
                offset += hEncoder->srInfo->cb_width_long[sb];
            }
            coderInfo[channel].sfb_offset[sb] = offset;
        }
    }

    for (channel = 0; channel < numChannels; channel++) {
        if ((channelInfo[channel].type != ELEMENT_LFE) && (useTns)) {
            TnsEncode(&(coderInfo[channel].tnsInfo), coderInfo[channel].sfbn, coderInfo[channel].sfbn, coderInfo[channel].block_type, coderInfo[channel].sfb_offset, hEncoder->freqBuff[channel], hEncoder->gpsyInfo.sharedWorkBuffLong);
        } else {
            coderInfo[channel].tnsInfo.tnsDataPresent = 0;
        }
    }

    for (channel = 0; channel < numChannels; channel++) {
		if (channelInfo[channel].type == ELEMENT_LFE) coderInfo[channel].sfbn = 3;
	}

    AACstereo(coderInfo, channelInfo, hEncoder->freqBuff, numChannels, (faac_real)hEncoder->aacquantCfg.quality/DEFQUAL, jointmode);

    for (channel = 0; channel < numChannels; channel++) {
        BlocQuant(&coderInfo[channel], hEncoder->freqBuff[channel], &(hEncoder->aacquantCfg));
    }

    for (channel = 0; channel < numChannels; channel++) {
		if (channelInfo[channel].present && (channelInfo[channel].type == ELEMENT_CPE) && (channelInfo[channel].ch_is_left)) {
			CoderInfo *cil, *cir;
			cil = &coderInfo[channel];
			cir = &coderInfo[channelInfo[channel].paired_ch];
            cil->sfbn = cir->sfbn = max(cil->sfbn, cir->sfbn);
		}
    }

    bitStream = OpenBitStream(bufferSize, outputBuffer);
    if (WriteBitstream(hEncoder, coderInfo, channelInfo, bitStream, numChannels) < 0) return -1;
    frameBytes = CloseBitStream(bitStream);

    if (hEncoder->config.bitRate) {
        int desbits = numChannels * (hEncoder->config.bitRate * FRAME_LEN) / hEncoder->sampleRate;
        int totalBits = frameBytes * 8;
        int sbrBits = 0;

        if (hEncoder->config.aacObjectType == HE_AAC && hEncoder->sbrInfo) {
            int id_aac = (numChannels > 1) ? ID_CPE : ID_SCE;
            sbrBits = SBRWriteBitstream(hEncoder->sbrInfo, NULL, id_aac, 0);
        }

        /* Adjust quality based on core bit performance against its allocated budget.
         * Subtracting SBR overhead prevents the rate controller from over-starving
         * the core when SBR consumes its fixed allocation. */
        faac_real fix;
        if (totalBits > sbrBits) {
            fix = (faac_real)(desbits - sbrBits) / (faac_real)(totalBits - sbrBits);
        } else {
            fix = 1.0;
        }

        if (fix < (1.0 - RC_DEADBAND_THRESHOLD)) fix += RC_DEADBAND_THRESHOLD;
        else if (fix > (1.0 + RC_DEADBAND_THRESHOLD)) fix -= RC_DEADBAND_THRESHOLD;
        else fix = 1.0;

        fix = (fix - 1.0) * RC_DAMPING_FACTOR + 1.0;
        hEncoder->aacquantCfg.quality *= fix;

        if (hEncoder->aacquantCfg.quality > maxqual) hEncoder->aacquantCfg.quality = maxqual;
        if (hEncoder->aacquantCfg.quality < MINQUAL) hEncoder->aacquantCfg.quality = MINQUAL;
    }

    return frameBytes;
}

static SR_INFO srInfo[12+1] =
{
    { 96000, 41, 12,
        {
            4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
            8, 8, 8, 8, 8, 12, 12, 12, 12, 12, 16, 16, 24, 28,
            36, 44, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64
        },{
            4, 4, 4, 4, 4, 4, 8, 8, 8, 16, 28, 36
        }
    }, { 88200, 41, 12,
        {
            4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
            8, 8, 8, 8, 8, 12, 12, 12, 12, 12, 16, 16, 24, 28,
            36, 44, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64
        },{
            4, 4, 4, 4, 4, 4, 8, 8, 8, 16, 28, 36
        }
    }, { 64000, 47, 12,
        {
            4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
            8, 8, 8, 8, 12, 12, 12, 16, 16, 16, 20, 24, 24, 28,
            36, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40,
            40, 40, 40, 40, 40
        },{
            4, 4, 4, 4, 4, 4, 8, 8, 8, 16, 28, 32
        }
    }, { 48000, 49, 14,
        {
            4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  8,  8,  8,  8,  8,  8,  8,
            12, 12, 12, 12, 16, 16, 20, 20, 24, 24, 28, 28, 32, 32, 32, 32, 32, 32,
            32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 96
        }, {
            4,  4,  4,  4,  4,  8,  8,  8, 12, 12, 12, 16, 16, 16
        }
    }, { 44100, 49, 14,
        {
            4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  8,  8,  8,  8,  8,  8,  8,
            12, 12, 12, 12, 16, 16, 20, 20, 24, 24, 28, 28, 32, 32, 32, 32, 32, 32,
            32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 96
        }, {
            4,  4,  4,  4,  4,  8,  8,  8, 12, 12, 12, 16, 16, 16
        }
    }, { 32000, 51, 14,
        {
            4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  8,  8,  8,  8,
            8,  8,  8,  12, 12, 12, 12, 16, 16, 20, 20, 24, 24, 28,
            28, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32,
            32, 32, 32, 32, 32, 32, 32, 32, 32
        },{
            4,  4,  4,  4,  4,  8,  8,  8,  12, 12, 12, 16, 16, 16
        }
    }, { 24000, 47, 15,
        {
            4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  8,  8,  8,  8,  8,  8,  8,
            8,  8,  8,  12, 12, 12, 12, 16, 16, 16, 20, 20, 24, 24, 28, 28, 32,
            36, 36, 40, 44, 48, 52, 52, 64, 64, 64, 64, 64
        }, {
            4,  4,  4,  4,  4,  4,  4,  8,  8,  8, 12, 12, 16, 16, 20
        }
    }, { 22050, 47, 15,
        {
            4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  8,  8,  8,  8,  8,  8,  8,
            8,  8,  8,  12, 12, 12, 12, 16, 16, 16, 20, 20, 24, 24, 28, 28, 32,
            36, 36, 40, 44, 48, 52, 52, 64, 64, 64, 64, 64
        }, {
            4,  4,  4,  4,  4,  4,  4,  8,  8,  8, 12, 12, 16, 16, 20
        }
    }, { 16000, 43, 15,
        {
            8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 12, 12, 12,
            12, 12, 12, 12, 12, 12, 16, 16, 16, 16, 20, 20, 20, 24,
            24, 28, 28, 32, 36, 40, 40, 44, 48, 52, 56, 60, 64, 64, 64
        }, {
            4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 12, 12, 16, 20, 20
        }
    }, { 12000, 43, 15,
        {
            8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 12, 12, 12,
            12, 12, 12, 12, 12, 12, 16, 16, 16, 16, 20, 20, 20, 24,
            24, 28, 28, 32, 36, 40, 40, 44, 48, 52, 56, 60, 64, 64, 64
        }, {
            4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 12, 12, 16, 20, 20
        }
    }, { 11025, 43, 15,
        {
            8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 12, 12, 12,
            12, 12, 12, 12, 12, 12, 16, 16, 16, 16, 20, 20, 20, 24,
            24, 28, 28, 32, 36, 40, 40, 44, 48, 52, 56, 60, 64, 64, 64
        }, {
            4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 12, 12, 16, 20, 20
        }
    }, { 8000, 40, 15,
        {
            12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 16,
            16, 16, 16, 16, 16, 16, 20, 20, 20, 20, 24, 24, 24, 28,
            28, 32, 36, 36, 40, 44, 48, 52, 56, 60, 64, 80
        }, {
            4, 4, 4, 4, 4, 4, 4, 8, 8, 8, 8, 12, 16, 20, 20
        }
    },
    { -1 }
};
