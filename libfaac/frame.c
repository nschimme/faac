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

#include "frame.h"
#include "coder.h"
#include "channels.h"
#include "bitstream.h"
#include "filtbank.h"
#include "util.h"
#include "tns.h"
#include "stereo.h"

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

#ifdef DRM
    config->pnslevel = 0;
#endif

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

#ifdef DRM
    *maxOutputBytes += 1; /* for CRC */
#endif

    hEncoder = (faacEncStruct*)AllocMemory(sizeof(faacEncStruct));
    SetMemory(hEncoder, 0, sizeof(faacEncStruct));

    hEncoder->numChannels = numChannels;
    hEncoder->sampleRate = sampleRate;
    hEncoder->sampleRateIdx = GetSRIndex(sampleRate);

    /* Initialize variables to default values */
    hEncoder->frameNum = 0;
    hEncoder->flushFrame = 0;

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

    for (channel = 0; channel < MAX_CHANNELS; channel++) {
        hEncoder->last_frame_energy[channel] = 0.0;
    }

    for (channel = 0; channel < numChannels; channel++)
	{
        hEncoder->coderInfo[channel].prev_window_shape = SINE_WINDOW;
        hEncoder->coderInfo[channel].window_shape = SINE_WINDOW;
        hEncoder->coderInfo[channel].block_type = ONLY_LONG_WINDOW;
        hEncoder->coderInfo[channel].groups.n = 1;
        hEncoder->coderInfo[channel].groups.len[0] = 1;

        for (int i = 0; i < NSFB_LONG; i++)
            hEncoder->coderInfo[channel].thr_adj[i] = 1.0;

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

/* Phase 2 Modular Hooks */

static void detect_transient(faacEncStruct *hEncoder, frame_analysis_t *analysis)
{
    const faac_real TRANSIENT_THRESHOLD = 5.0;
    if (analysis->transient_score > TRANSIENT_THRESHOLD) {
        /* Force short block for NEXT frame */
        for (unsigned int channel = 0; channel < hEncoder->numChannels; channel++) {
             hEncoder->psyInfo[channel].block_type = ONLY_SHORT_WINDOW;
        }
    }
}

static void apply_tonality_mask(faacEncStruct *hEncoder, frame_analysis_t *analysis)
{
    unsigned int channel;
    int sfb;
    const faac_real TONAL_THRESHOLD = 0.5;

    for (channel = 0; channel < hEncoder->numChannels; channel++) {
        CoderInfo *coder = &hEncoder->coderInfo[channel];
        for (sfb = 0; sfb < coder->sfbn; sfb++) {
            if (analysis->band_tonality[sfb] > TONAL_THRESHOLD) {
                coder->thr_adj[sfb] = 0.90;
            } else {
                coder->thr_adj[sfb] = 1.10;
            }
        }
    }
}

static void limit_hf(faacEncStruct *hEncoder, frame_analysis_t *analysis)
{
    unsigned int channel;
    int sfb;
    const faac_real HF_THRESHOLD = 1e-4;
    int max_sfb = 0;

    for (sfb = 0; sfb < NSFB_LONG; sfb++) {
        if (analysis->band_energy[sfb] > HF_THRESHOLD) {
            max_sfb = sfb + 1;
        }
    }

    if (max_sfb > 0) {
        for (channel = 0; channel < hEncoder->numChannels; channel++) {
            CoderInfo *coder = &hEncoder->coderInfo[channel];
            if (coder->sfbn > max_sfb) {
                coder->sfbn = max_sfb;
            }
        }
    }
}

static void adjust_reservoir(faacEncStruct *hEncoder, frame_analysis_t *analysis)
{
    faac_real hf_energy = 0;
    faac_real complexity;
    int sfb;
    int start_hf = 30;

    for (sfb = start_hf; sfb < NSFB_LONG; sfb++) {
        hf_energy += analysis->band_energy[sfb];
    }

    complexity = analysis->transient_score * analysis->spectral_flatness;
    if (hf_energy > 1.0) complexity *= 1.1;

    if (complexity > 2.0) {
        hEncoder->aacquantCfg.quality *= 1.01;
    } else if (complexity < 0.5) {
        hEncoder->aacquantCfg.quality *= 0.99;
    }

    if (hEncoder->aacquantCfg.quality > 1000) hEncoder->aacquantCfg.quality = 1000;
    if (hEncoder->aacquantCfg.quality < 10) hEncoder->aacquantCfg.quality = 10;
}

static void quantizer_search_tweak(faacEncStruct *hEncoder, frame_analysis_t *analysis)
{
    /* RD search is currently integrated inside BlocQuant/qlevel */
}

/* Pipeline Stages */

static void analysis_update(faacEncStruct *hEncoder)
{
    unsigned int channel;
    unsigned int bandWidth = hEncoder->config.bandWidth;
    unsigned int numChannels = hEncoder->numChannels;

    for (channel = 0; channel < numChannels; channel++)
    {
        if (!hEncoder->channelInfo[channel].lfe || hEncoder->channelInfo[channel].cpe)
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

    hEncoder->psymodel->PsyCalculate(hEncoder->channelInfo, &hEncoder->gpsyInfo, hEncoder->psyInfo,
        hEncoder->srInfo->cb_width_long, hEncoder->srInfo->num_cb_long,
        hEncoder->srInfo->cb_width_short,
        hEncoder->srInfo->num_cb_short, hEncoder->numChannels, (faac_real)hEncoder->aacquantCfg.quality / DEFQUAL);

    hEncoder->psymodel->BlockSwitch(hEncoder->coderInfo, hEncoder->psyInfo, numChannels);

    /* force block type */
    if (hEncoder->config.shortctl == SHORTCTL_NOSHORT)
    {
        for (channel = 0; channel < numChannels; channel++)
            hEncoder->coderInfo[channel].block_type = ONLY_LONG_WINDOW;
    }
    else if ((hEncoder->frameNum <= 4) || (hEncoder->config.shortctl == SHORTCTL_NOLONG))
    {
        for (channel = 0; channel < numChannels; channel++)
            hEncoder->coderInfo[channel].block_type = ONLY_SHORT_WINDOW;
    }
}

static void compute_masking(faacEncStruct *hEncoder)
{
    /* FilterBank + SFB Init */
    unsigned int channel;
    unsigned int numChannels = hEncoder->numChannels;

    for (channel = 0; channel < numChannels; channel++) {
        FilterBank(hEncoder, &hEncoder->coderInfo[channel], hEncoder->sampleBuff[channel],
            hEncoder->freqBuff[channel], hEncoder->overlapBuff[channel], MOVERLAPPED);
    }

    for (channel = 0; channel < numChannels; channel++) {
        int sb;
        unsigned int offset = 0;
        hEncoder->channelInfo[channel].msInfo.is_present = 0;
        if (hEncoder->coderInfo[channel].block_type == ONLY_SHORT_WINDOW) {
            hEncoder->coderInfo[channel].sfbn = hEncoder->aacquantCfg.max_cbs;
            for (sb = 0; sb < hEncoder->coderInfo[channel].sfbn; sb++) {
                hEncoder->coderInfo[channel].sfb_offset[sb] = offset;
                offset += hEncoder->srInfo->cb_width_short[sb];
            }
            hEncoder->coderInfo[channel].sfb_offset[sb] = offset;
            BlocGroup(hEncoder->freqBuff[channel], hEncoder->coderInfo + channel, &hEncoder->aacquantCfg);
        } else {
            hEncoder->coderInfo[channel].sfbn = hEncoder->aacquantCfg.max_cbl;
            hEncoder->coderInfo[channel].groups.n = 1;
            hEncoder->coderInfo[channel].groups.len[0] = 1;
            for (sb = 0; sb < hEncoder->coderInfo[channel].sfbn; sb++) {
                hEncoder->coderInfo[channel].sfb_offset[sb] = offset;
                offset += hEncoder->srInfo->cb_width_long[sb];
            }
            hEncoder->coderInfo[channel].sfb_offset[sb] = offset;
        }
    }
}

static void analysis_finish(faacEncStruct *hEncoder, frame_analysis_t *analysis)
{
    unsigned int channel;
    unsigned int numChannels = hEncoder->numChannels;
    faac_real total_transient_score = 0;
    faac_real total_spectral_flatness = 0;
    faac_real eps = 1e-9;
    int sfb;

    memset(analysis->band_energy, 0, sizeof(analysis->band_energy));
    memset(analysis->band_tonality, 0, sizeof(analysis->band_tonality));
    analysis->spectral_flatness = 0;
    analysis->transient_score = 0;

    for (channel = 0; channel < numChannels; channel++) {
        faac_real energy_ch = 0;
        faac_real log_sum_ch = 0;
        faac_real abs_sum_ch = 0;
        faac_real *xr = hEncoder->freqBuff[channel];
        int sfbn = hEncoder->coderInfo[channel].sfbn;

        if (hEncoder->coderInfo[channel].block_type == ONLY_SHORT_WINDOW) {
            for (sfb = 0; sfb < sfbn; sfb++) {
                faac_real b_sum = 0;
                faac_real b_abs_sum = 0;
                faac_real b_en = 0;
                int width = hEncoder->srInfo->cb_width_short[sfb];
                int start = hEncoder->coderInfo[channel].sfb_offset[sfb];
                int w, k;

                for (w = 0; w < 8; w++) {
                    faac_real *window_xr = xr + w * BLOCK_LEN_SHORT;
                    for (k = 0; k < width; k++) {
                        faac_real val = window_xr[start + k];
                        faac_real aval = (faac_real)FAAC_FABS(val);
                        b_sum += val;
                        b_abs_sum += aval;
                        b_en += val * val;
                        log_sum_ch += (faac_real)FAAC_LOG(aval + eps);
                        abs_sum_ch += aval;
                    }
                }
                energy_ch += b_en;
                analysis->band_energy[sfb] += b_en;
                analysis->band_tonality[sfb] += (faac_real)FAAC_FABS(b_sum) / (b_abs_sum + eps);
            }
        } else {
            for (sfb = 0; sfb < sfbn; sfb++) {
                faac_real b_sum = 0;
                faac_real b_abs_sum = 0;
                faac_real b_en = 0;
                int start = hEncoder->coderInfo[channel].sfb_offset[sfb];
                int end = hEncoder->coderInfo[channel].sfb_offset[sfb+1];
                int k;

                for (k = start; k < end; k++) {
                    faac_real val = xr[k];
                    faac_real aval = (faac_real)FAAC_FABS(val);
                    b_sum += val;
                    b_abs_sum += aval;
                    b_en += val * val;
                    log_sum_ch += (faac_real)FAAC_LOG(aval + eps);
                    abs_sum_ch += aval;
                }
                energy_ch += b_en;
                analysis->band_energy[sfb] += b_en;
                analysis->band_tonality[sfb] += (faac_real)FAAC_FABS(b_sum) / (b_abs_sum + eps);
            }
        }

        /* Per-channel metrics */
        {
            faac_real geom_mean = (faac_real)FAAC_EXP(log_sum_ch / FRAME_LEN);
            faac_real arith_mean = abs_sum_ch / FRAME_LEN;
            total_spectral_flatness += geom_mean / (arith_mean + eps);

            total_transient_score += energy_ch / (hEncoder->last_frame_energy[channel] + eps);
            hEncoder->last_frame_energy[channel] = energy_ch;
        }
    }

    /* Average across channels */
    analysis->transient_score = total_transient_score / numChannels;
    analysis->spectral_flatness = total_spectral_flatness / numChannels;
    for (sfb = 0; sfb < NSFB_LONG; sfb++) {
        analysis->band_energy[sfb] /= numChannels;
        analysis->band_tonality[sfb] /= numChannels;
    }
}

static void quantize_bands(faacEncStruct *hEncoder)
{
    unsigned int channel;
    unsigned int numChannels = hEncoder->numChannels;

    /* Stereo + TNS (Phase 2 corrected order) */
    AACstereo(hEncoder->coderInfo, hEncoder->channelInfo, hEncoder->freqBuff, numChannels,
              (faac_real)hEncoder->aacquantCfg.quality/DEFQUAL, hEncoder->config.jointmode);

    for (channel = 0; channel < numChannels; channel++) {
        if (!hEncoder->channelInfo[channel].lfe && hEncoder->config.useTns) {
            TnsEncode(&(hEncoder->coderInfo[channel].tnsInfo), hEncoder->coderInfo[channel].sfbn, hEncoder->coderInfo[channel].sfbn,
                      hEncoder->coderInfo[channel].block_type, hEncoder->coderInfo[channel].sfb_offset,
                      hEncoder->freqBuff[channel], hEncoder->gpsyInfo.sharedWorkBuffLong);
        } else {
            hEncoder->coderInfo[channel].tnsInfo.tnsDataPresent = 0;
        }
    }

    /* BlocQuant */
    for (channel = 0; channel < numChannels; channel++) {
        BlocQuant(&hEncoder->coderInfo[channel], hEncoder->freqBuff[channel], &hEncoder->aacquantCfg);
    }

    /* Fix SFB limit for CPE */
    for (channel = 0; channel < numChannels; channel++) {
        if (hEncoder->channelInfo[channel].present && hEncoder->channelInfo[channel].cpe && hEncoder->channelInfo[channel].ch_is_left) {
            int paired = hEncoder->channelInfo[channel].paired_ch;
            hEncoder->coderInfo[channel].sfbn = hEncoder->coderInfo[paired].sfbn = max(hEncoder->coderInfo[channel].sfbn, hEncoder->coderInfo[paired].sfbn);
        }
    }

    /* LFE bandwidth */
    for (channel = 0; channel < numChannels; channel++) {
        if (!hEncoder->channelInfo[channel].cpe && hEncoder->channelInfo[channel].lfe) hEncoder->coderInfo[channel].sfbn = 3;
    }
}

static int write_bitstream(faacEncStruct *hEncoder, unsigned int bufferSize, unsigned char *outputBuffer, int *frameBytes)
{
    BitStream *bitStream = OpenBitStream(bufferSize, outputBuffer);
    if (WriteBitstream(hEncoder, hEncoder->coderInfo, hEncoder->channelInfo, bitStream, hEncoder->numChannels) < 0) return -1;
    *frameBytes = CloseBitStream(bitStream);

    /* Quality adjustment for CBR-ish */
    if (hEncoder->config.bitRate) {
        int maxqual = hEncoder->config.outputFormat ? MAXQUALADTS : MAXQUAL;
        int desbits = hEncoder->numChannels * (hEncoder->config.bitRate * FRAME_LEN) / hEncoder->sampleRate;
        faac_real fix = (faac_real)desbits / (faac_real)(*frameBytes * 8);
        if (fix < 0.9) fix += 0.1; else if (fix > 1.1) fix -= 0.1; else fix = 1.0;
        fix = (fix - 1.0) * 0.5 + 1.0;
        hEncoder->aacquantCfg.quality *= fix;
        if (hEncoder->aacquantCfg.quality > maxqual) hEncoder->aacquantCfg.quality = maxqual;
        if (hEncoder->aacquantCfg.quality < 10) hEncoder->aacquantCfg.quality = 10;
    }

    return 0;
}

static int encode_frame(faacEncStruct *hEncoder, unsigned int bufferSize, unsigned char *outputBuffer, frame_analysis_t *analysis)
{
    int frameBytes = 0;

    analysis_update(hEncoder);

    if (hEncoder->frameNum <= 3)
        return 0;

    compute_masking(hEncoder);
    analysis_finish(hEncoder, analysis);

    /* Modular Behavioral Pipeline (Task 6) */
    detect_transient(hEncoder, analysis);
    apply_tonality_mask(hEncoder, analysis);
    limit_hf(hEncoder, analysis);
    adjust_reservoir(hEncoder, analysis);
    quantizer_search_tweak(hEncoder, analysis);

    quantize_bands(hEncoder);

    if (write_bitstream(hEncoder, bufferSize, outputBuffer, &frameBytes) < 0) return -1;

    return frameBytes;
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
    int frameBytes = 0;

    /* local copy's of parameters */
    ChannelInfo *channelInfo = hEncoder->channelInfo;
    unsigned int numChannels = hEncoder->numChannels;
    unsigned int useLfe = hEncoder->config.useLfe;

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
    }

#ifdef DRM
    {
        unsigned int jointmode = hEncoder->config.jointmode;
        frame_analysis_t analysis;
        int diff = 1;

        memset(&analysis, 0, sizeof(frame_analysis_t));

        analysis_update(hEncoder);

        if (hEncoder->frameNum <= 3) {
            return 0;
        }

        compute_masking(hEncoder);
        analysis_finish(hEncoder, &analysis);

        /* Modular Behavioral Pipeline */
        detect_transient(hEncoder, &analysis);
        apply_tonality_mask(hEncoder, &analysis);
        limit_hf(hEncoder, &analysis);
        adjust_reservoir(hEncoder, &analysis);
        quantizer_search_tweak(hEncoder, &analysis);

        /* DRM loop: iteratively adjust quality via quantization and bitstream writing */
        while (diff > 0) {
            quantize_bands(hEncoder);

            /* Write the AAC bitstream */
            BitStream *bitStream = OpenBitStream(bufferSize, outputBuffer);
            WriteBitstream(hEncoder, hEncoder->coderInfo, hEncoder->channelInfo, bitStream, hEncoder->numChannels);

            /* Close the bitstream and return the number of bytes written */
            frameBytes = CloseBitStream(bitStream);

            /* now calculate desired bits and compare with actual encoded bits */
            int desbits = (int) ((faac_real) hEncoder->numChannels * (hEncoder->config.bitRate * FRAME_LEN)
                    / hEncoder->sampleRate);

            diff = ((frameBytes - 1 /* CRC */) * 8) - desbits;

            /* do linear correction according to relative difference */
            faac_real fix = (faac_real) desbits / ((frameBytes - 1 /* CRC */) * 8);

            /* speed up convergence. A value of 0.92 gives approx up to 10 iterations */
            if (fix > 0.92)
                fix = 0.92;

            hEncoder->aacquantCfg.quality *= fix;

            /* quality should not go lower than 1, set diff to exit loop */
            if (hEncoder->aacquantCfg.quality <= 1)
                diff = -1;
        }
    }
#else
    {
        frame_analysis_t analysis;
        memset(&analysis, 0, sizeof(frame_analysis_t));
        frameBytes = encode_frame(hEncoder, bufferSize, outputBuffer, &analysis);
    }
#endif

    return frameBytes;
}


#ifdef DRM
/* Scalefactorband data table for 960 transform length */
/* all parameters which are different from the 1024 transform length table are
   marked with an "x" */
static SR_INFO srInfo[12+1] =
{
    { 96000, 40/*x*/, 12,
        {
            4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
            8, 8, 8, 8, 8, 12, 12, 12, 12, 12, 16, 16, 24, 28,
            36, 44, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 0/*x*/
        },{
            4, 4, 4, 4, 4, 4, 8, 8, 8, 16, 28, 28/*x*/
        }
    }, { 88200, 40/*x*/, 12,
        {
            4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
            8, 8, 8, 8, 8, 12, 12, 12, 12, 12, 16, 16, 24, 28,
            36, 44, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 0/*x*/
        },{
            4, 4, 4, 4, 4, 4, 8, 8, 8, 16, 28, 28/*x*/
        }
    }, { 64000, 45/*x*/, 12,
        {
            4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
            8, 8, 8, 8, 12, 12, 12, 16, 16, 16, 20, 24, 24, 28,
            36, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40,
            40, 40, 40, 16/*x*/, 0/*x*/
        },{
            4, 4, 4, 4, 4, 4, 8, 8, 8, 16, 28, 28/*x*/
        }
    }, { 48000, 49, 14,
        {
            4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  8,  8,  8,  8,  8,  8,  8,
            12, 12, 12, 12, 16, 16, 20, 20, 24, 24, 28, 28, 32, 32, 32, 32, 32, 32,
            32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32/*x*/
        }, {
            4,  4,  4,  4,  4,  8,  8,  8, 12, 12, 12, 16, 16, 8/*x*/
        }
    }, { 44100, 49, 14,
        {
            4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  8,  8,  8,  8,  8,  8,  8,
            12, 12, 12, 12, 16, 16, 20, 20, 24, 24, 28, 28, 32, 32, 32, 32, 32, 32,
            32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32/*x*/
        }, {
            4,  4,  4,  4,  4,  8,  8,  8, 12, 12, 12, 16, 16, 8/*x*/
        }
    }, { 32000, 49/*x*/, 14,
        {
            4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  8,  8,  8,  8,
            8,  8,  8,  12, 12, 12, 12, 16, 16, 20, 20, 24, 24, 28,
            28, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32,
            32, 32, 32, 32, 32, 32, 32, 0/*x*/, 0/*x*/
        },{
            4,  4,  4,  4,  4,  8,  8,  8,  12, 12, 12, 16, 16, 16
        }
    }, { 24000, 46/*x*/, 15,
        {
            4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  8,  8,  8,  8,  8,  8,  8,
            8,  8,  8,  12, 12, 12, 12, 16, 16, 16, 20, 20, 24, 24, 28, 28, 32,
            36, 36, 40, 44, 48, 52, 52, 64, 64, 64, 64, 0/*x*/
        }, {
            4,  4,  4,  4,  4,  4,  4,  8,  8,  8, 12, 12, 16, 16, 12/*x*/
        }
    }, { 22050, 46/*x*/, 15,
        {
            4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  8,  8,  8,  8,  8,  8,  8,
            8,  8,  8,  12, 12, 12, 12, 16, 16, 16, 20, 20, 24, 24, 28, 28, 32,
            36, 36, 40, 44, 48, 52, 52, 64, 64, 64, 64, 0/*x*/
        }, {
            4,  4,  4,  4,  4,  4,  4,  8,  8,  8, 12, 12, 16, 16, 12/*x*/
        }
    }, { 16000, 42/*x*/, 15,
        {
            8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 12, 12, 12,
            12, 12, 12, 12, 12, 12, 16, 16, 16, 16, 20, 20, 20, 24,
            24, 28, 28, 32, 36, 40, 40, 44, 48, 52, 56, 60, 64, 64, 0/*x*/
        }, {
            4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 12, 12, 16, 20, 12/*x*/
        }
    }, { 12000, 42/*x*/, 15,
        {
            8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 12, 12, 12,
            12, 12, 12, 12, 12, 12, 16, 16, 16, 16, 20, 20, 20, 24,
            24, 28, 28, 32, 36, 40, 40, 44, 48, 52, 56, 60, 64, 64, 0/*x*/
        }, {
            4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 12, 12, 16, 20, 12/*x*/
        }
    }, { 11025, 42/*x*/, 15,
        {
            8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 12, 12, 12,
            12, 12, 12, 12, 12, 12, 16, 16, 16, 16, 20, 20, 20, 24,
            24, 28, 28, 32, 36, 40, 40, 44, 48, 52, 56, 60, 64, 64, 0/*x*/
        }, {
            4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 12, 12, 16, 20, 12/*x*/
        }
    }, { 8000, 40, 15,
        {
            12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 16,
            16, 16, 16, 16, 16, 16, 20, 20, 20, 20, 24, 24, 24, 28,
            28, 32, 36, 36, 40, 44, 48, 52, 56, 60, 64, 16/*x*/
        }, {
            4, 4, 4, 4, 4, 4, 4, 8, 8, 8, 8, 12, 16, 20, 12/*x*/
        }
    },
    { -1 }
};
#else
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
#endif
