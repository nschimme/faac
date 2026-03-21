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

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <math.h>

#include "frame.h"
#include "coder.h"
#include "channels.h"
#include "bitstream.h"
#include "filtbank.h"
#include "util.h"
#include "tns.h"
#include "stereo.h"
#include "huff2.h"

#if (defined WIN32 || defined _WIN32 || defined WIN64 || defined _WIN64) && !defined(PACKAGE_VERSION)
#include "win32_ver.h"
#endif

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

// default bandwidth/samplerate ratio
static const struct {
    faac_real fac;
    faac_real freq;
} g_bw = {0.42, 18000};

#ifndef min
#define min(a,b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a,b) ((a) > (b) ? (a) : (b))
#endif

/**
 * Pseudo-SBR: Bandwidth Extension for FAAC-LC
 * Objective: Enhance low-bitrate quality via spectral folding.
 * Replicates high-frequency content in the MDCT domain.
 */
static void PseudoSBR(faacEncHandle hpEncoder, CoderInfo *coder, faac_real *freq, int block_len)
{
    faacEncStruct* hEncoder = (faacEncStruct*)hpEncoder;
    int old_sfbn = coder->sfbn;
    int max_avail_sfbn = (block_len == FRAME_LEN) ? hEncoder->srInfo->num_cb_long : hEncoder->srInfo->num_cb_short;

    /* Extension: 2 bands for long, 1 for short to minimize side-info overhead */
    int num_ext = (block_len == FRAME_LEN) ? 2 : 1;
    int new_sfbn = min(old_sfbn + num_ext, max_avail_sfbn);
    int g, b, bin, w;

    if (new_sfbn <= old_sfbn) return;

    /* Expand sfbn and shift metadata for grouped windows to maintain alignment */
    if (coder->groups.n > 1) {
        for (g = coder->groups.n - 1; g >= 0; g--) {
            int old_base = g * old_sfbn;
            int new_base = g * new_sfbn;
            for (b = old_sfbn - 1; b >= 0; b--) {
                coder->book[new_base + b] = coder->book[old_base + b];
                coder->sf[new_base + b] = coder->sf[old_base + b];
            }
            /* Initialize newly created bands */
            for (b = old_sfbn; b < new_sfbn; b++) {
                coder->book[new_base + b] = HCB_NONE;
                coder->sf[new_base + b] = 0;
            }
        }
    }
    coder->sfbn = new_sfbn;

    int win_offset = 0;
    for (g = 0; g < coder->groups.n; g++)
    {
        for (b = old_sfbn; b < new_sfbn; b++)
        {
            int sfcnt = g * coder->sfbn + b;
            int start = coder->sfb_offset[b];
            int end = coder->sfb_offset[b+1];

            for (w = 0; w < coder->groups.len[g]; w++)
            {
                faac_real *wfreq = freq + (win_offset + w) * block_len;

                /* Adaptive Tilt Calculation via Spectral Flatness Measure (SFM) */
                int src_start = start / 2;
                int src_end = end / 2;
                faac_real sum_e = 0, sum_log_e = 0;
                int n_bins = src_end - src_start;
                for (bin = src_start; bin < src_end; bin++) {
                    faac_real e = wfreq[bin] * wfreq[bin] + 1e-10f;
                    sum_e += e;
                    sum_log_e += (faac_real)log(e);
                }
                faac_real sfm = (n_bins > 0) ? (faac_real)exp(sum_log_e / n_bins) / (sum_e / n_bins) : 0.3f;
                faac_real tilt_target = (sfm < 0.1f) ? -18.0f : (sfm > 0.5f ? -9.0f : -12.0f);

                for (bin = start; bin < end; bin++)
                {
                    int source_index = bin / 2;
                    faac_real bin_freq = (faac_real)bin * hEncoder->sampleRate / (block_len * 2);
                    faac_real crossover_freq = (faac_real)coder->sfb_offset[old_sfbn] * hEncoder->sampleRate / (block_len * 2);
                    faac_real tilt_db = tilt_target * (bin_freq - crossover_freq) / (16000.0f - crossover_freq);
                    if (tilt_db > 0) tilt_db = 0;

                    /* Folding Operation with Economy scaling (0.4) */
                    faac_real fold_val = wfreq[source_index] * (faac_real)pow(10.0, tilt_db / 20.0) * 0.4f;

                    /* Phase alignment: 5-bin cross-fade at crossover point */
                    if (b == old_sfbn && (bin - start) < 5) {
                        faac_real alpha = (faac_real)(bin - start) / 5.0f;
                        wfreq[bin] = wfreq[bin] * (1.0f - alpha) + fold_val * alpha;
                    } else {
                        wfreq[bin] = fold_val;
                    }
                    /* Noise Injection to prevent metallic harmonic artifacts */
                    wfreq[bin] += ((rand() / (faac_real)RAND_MAX * 2.0f - 1.0f) * 0.005f);
                }
            }
            /* Use a temporary book; BlocQuant/huffbook will optimize it */
            coder->book[sfcnt] = HCB_NONE;
            coder->sf[sfcnt] = 0;
        }
        win_offset += coder->groups.len[g];
    }
}

int FAACAPI faacEncGetVersion( char **faac_id_string,
			      				char **faac_copyright_string)
{
  if (faac_id_string)
    *faac_id_string = libfaacName;

  if (faac_copyright_string)
    *faac_copyright_string = libCopyright;

  return FAAC_CFG_VERSION;
}


int FAACAPI faacEncGetDecoderSpecificInfo(faacEncHandle hpEncoder,unsigned char** ppBuffer,unsigned long* pSizeOfDecoderSpecificInfo)
{
    faacEncStruct* hEncoder = (faacEncStruct*)hpEncoder;
    BitStream* pBitStream = NULL;

    if((hEncoder == NULL) || (ppBuffer == NULL) || (pSizeOfDecoderSpecificInfo == NULL)) {
        return -1;
    }

    if(hEncoder->config.mpegVersion == MPEG2){
        return -2; /* not supported */
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
    faacEncConfigurationPtr config = &(hEncoder->config);

    return config;
}

int FAACAPI faacEncSetConfiguration(faacEncHandle hpEncoder,
                                    faacEncConfigurationPtr config)
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

    assert((hEncoder->config.outputFormat == 0) || (hEncoder->config.outputFormat == 1));

    switch( hEncoder->config.inputFormat )
    {
        case FAAC_INPUT_16BIT:
        //case FAAC_INPUT_24BIT:
        case FAAC_INPUT_32BIT:
        case FAAC_INPUT_FLOAT:
            break;

        default:
            return 0;
            break;
    }

    if (hEncoder->config.aacObjectType != LOW)
        return 0;

    /* Re-init TNS for new profile */
    TnsInit(hEncoder);

    /* Check for correct bitrate */
    if (!hEncoder->sampleRate || !hEncoder->numChannels)
        return 0;
    if (config->bitRate > (MaxBitrate(hEncoder->sampleRate) / hEncoder->numChannels))
        config->bitRate = MaxBitrate(hEncoder->sampleRate) / hEncoder->numChannels;
#if 0
    if (config->bitRate < MinBitrate())
        return 0;
#endif

    if (config->bitRate && !config->bandWidth)
    {
        config->bandWidth = (faac_real)config->bitRate * hEncoder->sampleRate * g_bw.fac / 50000.0;
        if (config->bandWidth > g_bw.freq)
            config->bandWidth = g_bw.freq;

        if (!config->quantqual)
        {
            config->quantqual = (faac_real)config->bitRate * hEncoder->numChannels / 1280;
            if (config->quantqual > 100)
                config->quantqual = (config->quantqual - 100) * 3.0 + 100;
        }
    }

    if (!config->quantqual)
        config->quantqual = DEFQUAL;

    hEncoder->config.bitRate = config->bitRate;

    if (!config->bandWidth)
    {
        config->bandWidth = g_bw.fac * hEncoder->sampleRate;
    }

    hEncoder->config.bandWidth = config->bandWidth;

    // check bandwidth
    if (hEncoder->config.bandWidth < 100)
		hEncoder->config.bandWidth = 100;
    if (hEncoder->config.bandWidth > (hEncoder->sampleRate / 2))
		hEncoder->config.bandWidth = hEncoder->sampleRate / 2;

    if (config->quantqual > maxqual)
        config->quantqual = maxqual;
    if (config->quantqual < MINQUAL)
        config->quantqual = MINQUAL;

    hEncoder->config.quantqual = config->quantqual;

    if (config->jointmode == JOINT_MS)
        config->pnslevel = 0;
    if (config->pnslevel < 0)
        config->pnslevel = 0;
    if (config->pnslevel > 10)
        config->pnslevel = 10;
    hEncoder->aacquantCfg.pnslevel = config->pnslevel;
    /* set quantization quality */
    hEncoder->aacquantCfg.quality = config->quantqual;
    CalcBW(&hEncoder->config.bandWidth,
              hEncoder->sampleRate,
              hEncoder->srInfo,
              &hEncoder->aacquantCfg);

    /* Pseudo-SBR Activation Check */
    hEncoder->sbr_enabled = (hEncoder->config.bitRate && hEncoder->config.bitRate < 48000) ? 1 : 0;

    // reset psymodel
    hEncoder->psymodel->PsyEnd(&hEncoder->gpsyInfo, hEncoder->psyInfo, hEncoder->numChannels);
    if (config->psymodelidx >= (sizeof(psymodellist) / sizeof(psymodellist[0]) - 1))
		config->psymodelidx = (sizeof(psymodellist) / sizeof(psymodellist[0])) - 2;

    hEncoder->config.psymodelidx = config->psymodelidx;
    hEncoder->psymodel = (psymodel_t *)psymodellist[hEncoder->config.psymodelidx].ptr;
    hEncoder->psymodel->PsyInit(&hEncoder->gpsyInfo, hEncoder->psyInfo, hEncoder->numChannels,
			hEncoder->sampleRate, hEncoder->srInfo->cb_width_long,
			hEncoder->srInfo->num_cb_long, hEncoder->srInfo->cb_width_short,
			hEncoder->srInfo->num_cb_short);

	/* load channel_map */
	for( i = 0; i < MAX_CHANNELS; i++ )
		hEncoder->config.channel_map[i] = config->channel_map[i];

    /* OK */
    return 1;
}

faacEncHandle FAACAPI faacEncOpen(unsigned long sampleRate,
                                  unsigned int numChannels,
                                  unsigned long *inputSamples,
                                  unsigned long *maxOutputBytes)
{
    unsigned int channel;
    faacEncStruct* hEncoder;

    if (numChannels > MAX_CHANNELS)
	return NULL;

    *inputSamples = FRAME_LEN*numChannels;
    *maxOutputBytes = ADTS_FRAMESIZE;

    hEncoder = (faacEncStruct*)AllocMemory(sizeof(faacEncStruct));
    SetMemory(hEncoder, 0, sizeof(faacEncStruct));

    hEncoder->numChannels = numChannels;
    hEncoder->sampleRate = sampleRate;
    hEncoder->sampleRateIdx = GetSRIndex(sampleRate);

    /* Initialize variables to default values */
    hEncoder->frameNum = 0;
    hEncoder->flushFrame = 0;

    srand(0);

    /* Default configuration */
    hEncoder->config.version = FAAC_CFG_VERSION;
    hEncoder->config.name = libfaacName;
    hEncoder->config.copyright = libCopyright;
    hEncoder->config.mpegVersion = MPEG4;
    hEncoder->config.aacObjectType = LOW;
    hEncoder->config.jointmode = JOINT_IS;
    hEncoder->config.pnslevel = 4;
    hEncoder->config.useLfe = 1;
    hEncoder->config.useTns = 0;
    hEncoder->config.bitRate = 64000;
    hEncoder->config.bandWidth = g_bw.fac * hEncoder->sampleRate;
    hEncoder->config.quantqual = 0;
    hEncoder->config.psymodellist = (psymodellist_t *)psymodellist;
    hEncoder->config.psymodelidx = 0;
    hEncoder->psymodel =
      (psymodel_t *)hEncoder->config.psymodellist[hEncoder->config.psymodelidx].ptr;
    hEncoder->config.shortctl = SHORTCTL_NORMAL;

	/* default channel map is straight-through */
	for( channel = 0; channel < MAX_CHANNELS; channel++ )
		hEncoder->config.channel_map[channel] = channel;

    hEncoder->config.outputFormat = ADTS_STREAM;

    /*
        be compatible with software which assumes 24bit in 32bit PCM
    */
    hEncoder->config.inputFormat = FAAC_INPUT_32BIT;

    /* find correct sampling rate depending parameters */
    hEncoder->srInfo = &srInfo[hEncoder->sampleRateIdx];

    for (channel = 0; channel < numChannels; channel++)
	{
        hEncoder->coderInfo[channel].prev_window_shape = SINE_WINDOW;
        hEncoder->coderInfo[channel].window_shape = SINE_WINDOW;
        hEncoder->coderInfo[channel].block_type = ONLY_LONG_WINDOW;
        hEncoder->coderInfo[channel].groups.n = 1;
        hEncoder->coderInfo[channel].groups.len[0] = 1;

        hEncoder->sampleBuff[channel] = NULL;
    }

    /* Initialize coder functions */
	fft_initialize( &hEncoder->fft_tables );

	hEncoder->psymodel->PsyInit(&hEncoder->gpsyInfo, hEncoder->psyInfo, hEncoder->numChannels,
        hEncoder->sampleRate, hEncoder->srInfo->cb_width_long,
        hEncoder->srInfo->num_cb_long, hEncoder->srInfo->cb_width_short,
        hEncoder->srInfo->num_cb_short);

    FilterBankInit(hEncoder);

    TnsInit(hEncoder);

    QuantizeInit();

    /* Return handle */
    return hEncoder;
}

int FAACAPI faacEncClose(faacEncHandle hpEncoder)
{
    faacEncStruct* hEncoder = (faacEncStruct*)hpEncoder;
    unsigned int channel;

    /* Deinitialize coder functions */
    hEncoder->psymodel->PsyEnd(&hEncoder->gpsyInfo, hEncoder->psyInfo, hEncoder->numChannels);

    FilterBankEnd(hEncoder);

    fft_terminate(&hEncoder->fft_tables);

    /* Free remaining buffer memory */
    for (channel = 0; channel < hEncoder->numChannels; channel++)
	{
		if (hEncoder->sampleBuff[channel])
			FreeMemory(hEncoder->sampleBuff[channel]);
		if (hEncoder->next3SampleBuff[channel])
			FreeMemory (hEncoder->next3SampleBuff[channel]);
    }

    /* Free handle */
    if (hEncoder)
		FreeMemory(hEncoder);

    BlocStat();

    return 0;
}

int FAACAPI faacEncEncode(faacEncHandle hpEncoder,
                          int32_t *inputBuffer,
                          unsigned int samplesInput,
                          unsigned char *outputBuffer,
                          unsigned int bufferSize
                          )
{
    faacEncStruct* hEncoder = (faacEncStruct*)hpEncoder;
    unsigned int channel, i;
    int sb, frameBytes;
    unsigned int offset;
    BitStream *bitStream; /* bitstream used for writing the frame to */

    /* local copy's of parameters */
    ChannelInfo *channelInfo = hEncoder->channelInfo;
    CoderInfo *coderInfo = hEncoder->coderInfo;
    unsigned int numChannels = hEncoder->numChannels;
    unsigned int useLfe = hEncoder->config.useLfe;
    unsigned int useTns = hEncoder->config.useTns;
    unsigned int jointmode = hEncoder->config.jointmode;
    unsigned int bandWidth = hEncoder->config.bandWidth;
    unsigned int shortctl = hEncoder->config.shortctl;
    int maxqual = hEncoder->config.outputFormat ? MAXQUALADTS : MAXQUAL;

    /* Increase frame number */
    hEncoder->frameNum++;

    if (samplesInput == 0)
        hEncoder->flushFrame++;

    /* After 4 flush frames all samples have been encoded,
       return 0 bytes written */
    if (hEncoder->flushFrame > 4)
        return 0;

    /* Determine the channel configuration */
    GetChannelInfo(channelInfo, numChannels, useLfe);

    /* Update current sample buffers */
    for (channel = 0; channel < numChannels; channel++)
	{
		faac_real *tmp;


		if (!hEncoder->sampleBuff[channel])
			hEncoder->sampleBuff[channel] = (faac_real*)AllocMemory(FRAME_LEN*sizeof(faac_real));

		tmp = hEncoder->sampleBuff[channel];

		hEncoder->sampleBuff[channel]	= hEncoder->next3SampleBuff[channel];
		hEncoder->next3SampleBuff[channel]	= tmp;

        if (samplesInput == 0)
        {
            /* start flushing*/
            for (i = 0; i < FRAME_LEN; i++)
                hEncoder->next3SampleBuff[channel][i] = 0.0;
        }
        else
        {
			int samples_per_channel = samplesInput/numChannels;

            /* handle the various input formats and channel remapping */
            switch( hEncoder->config.inputFormat )
			{
                case FAAC_INPUT_16BIT:
					{
						short *input_channel = (short*)inputBuffer + hEncoder->config.channel_map[channel];

						for (i = 0; i < samples_per_channel; i++)
						{
							hEncoder->next3SampleBuff[channel][i] = (faac_real)*input_channel;
							input_channel += numChannels;
						}
					}
                    break;

                case FAAC_INPUT_32BIT:
					{
						int32_t *input_channel = (int32_t*)inputBuffer + hEncoder->config.channel_map[channel];

						for (i = 0; i < samples_per_channel; i++)
						{
							hEncoder->next3SampleBuff[channel][i] = (1.0/256) * (faac_real)*input_channel;
							input_channel += numChannels;
						}
					}
                    break;

                case FAAC_INPUT_FLOAT:
					{
						float *input_channel = (float*)inputBuffer + hEncoder->config.channel_map[channel];

						for (i = 0; i < samples_per_channel; i++)
						{
							hEncoder->next3SampleBuff[channel][i] = (faac_real)*input_channel;
							input_channel += numChannels;
						}
					}
                    break;

                default:
                    return -1; /* invalid input format */
                    break;
            }

            for (i = (int)(samplesInput/numChannels); i < FRAME_LEN; i++)
                hEncoder->next3SampleBuff[channel][i] = 0.0;
		}

		/* Psychoacoustics */
		/* Update buffers and run FFT on new samples */
		/* LFE psychoacoustic can run without it */
		if (!channelInfo[channel].lfe || channelInfo[channel].cpe)
		{
			hEncoder->psymodel->PsyBufferUpdate(
					&hEncoder->fft_tables,
					&hEncoder->gpsyInfo,
					&hEncoder->psyInfo[channel],
					hEncoder->next3SampleBuff[channel],
					bandWidth,
					hEncoder->srInfo->cb_width_short,
					hEncoder->srInfo->num_cb_short);
		}
    }

    if (hEncoder->frameNum <= 3) /* Still filling up the buffers */
        return 0;

    /* Psychoacoustics */
    hEncoder->psymodel->PsyCalculate(channelInfo, &hEncoder->gpsyInfo, hEncoder->psyInfo,
        hEncoder->srInfo->cb_width_long, hEncoder->srInfo->num_cb_long,
        hEncoder->srInfo->cb_width_short,
        hEncoder->srInfo->num_cb_short, numChannels, (faac_real)hEncoder->aacquantCfg.quality / DEFQUAL);

    hEncoder->psymodel->BlockSwitch(coderInfo, hEncoder->psyInfo, numChannels);

    /* force block type */
    if (shortctl == SHORTCTL_NOSHORT)
    {
		for (channel = 0; channel < numChannels; channel++)
		{
			coderInfo[channel].block_type = ONLY_LONG_WINDOW;
		}
    }
    else if ((hEncoder->frameNum <= 4) || (shortctl == SHORTCTL_NOLONG))
    {
		for (channel = 0; channel < numChannels; channel++)
		{
			coderInfo[channel].block_type = ONLY_SHORT_WINDOW;
		}
    }

    /* AAC Filterbank, MDCT with overlap and add */
    for (channel = 0; channel < numChannels; channel++) {
        FilterBank(hEncoder,
            &coderInfo[channel],
            hEncoder->sampleBuff[channel],
            hEncoder->freqBuff[channel],
            hEncoder->overlapBuff[channel],
            MOVERLAPPED);
    }

    for (channel = 0; channel < numChannels; channel++) {
        channelInfo[channel].msInfo.is_present = 0;

        if (coderInfo[channel].block_type == ONLY_SHORT_WINDOW) {
            coderInfo[channel].sfbn = hEncoder->aacquantCfg.max_cbs;

            offset = 0;
            for (sb = 0; sb < hEncoder->srInfo->num_cb_short; sb++) {
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
            for (sb = 0; sb < hEncoder->srInfo->num_cb_long; sb++) {
                coderInfo[channel].sfb_offset[sb] = offset;
                offset += hEncoder->srInfo->cb_width_long[sb];
            }
            coderInfo[channel].sfb_offset[sb] = offset;
        }
    }

    /* Perform TNS analysis and filtering */
    for (channel = 0; channel < numChannels; channel++) {
        if ((!channelInfo[channel].lfe) && (useTns)) {
            TnsEncode(&(coderInfo[channel].tnsInfo),
                      coderInfo[channel].sfbn,
                      coderInfo[channel].sfbn,
                      coderInfo[channel].block_type,
                      coderInfo[channel].sfb_offset,
                      hEncoder->freqBuff[channel], hEncoder->gpsyInfo.sharedWorkBuffLong);
        } else {
            coderInfo[channel].tnsInfo.tnsDataPresent = 0;      /* TNS not used for LFE */
        }
    }

    for (channel = 0; channel < numChannels; channel++) {
      // reduce LFE bandwidth
		if (!channelInfo[channel].cpe && channelInfo[channel].lfe)
		{
                    coderInfo[channel].sfbn = 3;
		}
	}

    AACstereo(coderInfo, channelInfo, hEncoder->freqBuff, numChannels,
              (faac_real)hEncoder->aacquantCfg.quality/DEFQUAL, jointmode);

    /* Pseudo-SBR Application */
    if (hEncoder->sbr_enabled)
    {
        int crossover_sfb[MAX_CHANNELS];
        for (channel = 0; channel < numChannels; channel++) {
            crossover_sfb[channel] = coderInfo[channel].sfbn;
        }

        for (channel = 0; channel < numChannels; channel++)
        {
            int block_type = coderInfo[channel].block_type;
            int block_len = (block_type == ONLY_SHORT_WINDOW) ? BLOCK_LEN_SHORT : BLOCK_LEN_LONG;
            PseudoSBR(hEncoder, &coderInfo[channel], hEncoder->freqBuff[channel], block_len);
        }

        /* Intensity Stereo Extension for SBR bands */
        for (channel = 0; channel < numChannels; channel++)
        {
            if (channelInfo[channel].present && channelInfo[channel].cpe && channelInfo[channel].ch_is_left)
            {
                int lch = channel;
                int rch = channelInfo[lch].paired_ch;
                int g_idx;
                for (g_idx = 0; g_idx < coderInfo[rch].groups.n; g_idx++)
                {
                    int last_real = crossover_sfb[rch] - 1;
                    if (last_real < 0) continue;
                    int r_last_sfcnt = g_idx * coderInfo[rch].sfbn + last_real;
                    int r_last_book = coderInfo[rch].book[r_last_sfcnt];

                    if (r_last_book == HCB_INTENSITY || r_last_book == HCB_INTENSITY2)
                    {
                        for (int b = crossover_sfb[rch]; b < coderInfo[rch].sfbn; b++)
                        {
                            int r_curr_sfcnt = g_idx * coderInfo[rch].sfbn + b;
                            coderInfo[rch].book[r_curr_sfcnt] = r_last_book;
                            coderInfo[rch].sf[r_curr_sfcnt] = coderInfo[rch].sf[r_last_sfcnt];
                        }
                    }
                }
            }
        }
    }

    for (channel = 0; channel < numChannels; channel++) {
        BlocQuant(&coderInfo[channel], hEncoder->freqBuff[channel],
                  &(hEncoder->aacquantCfg));
    }

    // fix max_sfb in CPE mode
    for (channel = 0; channel < numChannels; channel++)
    {
		if (channelInfo[channel].present
				&& (channelInfo[channel].cpe)
				&& (channelInfo[channel].ch_is_left))
		{
			CoderInfo *cil, *cir;

			cil = &coderInfo[channel];
			cir = &coderInfo[channelInfo[channel].paired_ch];

                        cil->sfbn = cir->sfbn = max(cil->sfbn, cir->sfbn);
		}
    }
    /* Write the AAC bitstream */
    bitStream = OpenBitStream(bufferSize, outputBuffer);

    if (WriteBitstream(hEncoder, coderInfo, channelInfo, bitStream, numChannels) < 0)
        return -1;

    /* Close the bitstream and return the number of bytes written */
    frameBytes = CloseBitStream(bitStream);

    /* Adjust quality to get correct average bitrate */
    if (hEncoder->config.bitRate)
    {
        int desbits = numChannels * (hEncoder->config.bitRate * FRAME_LEN)
            / hEncoder->sampleRate;
        faac_real fix = (faac_real)desbits / (faac_real)(frameBytes * 8);

        if (fix < 0.9)
            fix += 0.1;
        else if (fix > 1.1)
            fix -= 0.1;
        else
            fix = 1.0;

        fix = (fix - 1.0) * 0.5 + 1.0;
        // printf("q: %.1f(f:%.4f)\n", hEncoder->aacquantCfg.quality, fix);

        hEncoder->aacquantCfg.quality *= fix;

        if (hEncoder->aacquantCfg.quality > maxqual)
            hEncoder->aacquantCfg.quality = maxqual;
        if (hEncoder->aacquantCfg.quality < 10)
            hEncoder->aacquantCfg.quality = 10;
    }

    return frameBytes;
}


/* Scalefactorband data table for 1024 transform length */
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
