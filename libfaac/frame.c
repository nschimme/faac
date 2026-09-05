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
#include "quantize.h"
#include "util.h"
#include "tns.h"
#include "stereo.h"
#include "sbr.h"

/* HE-AAC auto-mode thresholds; tuned via ViSQOL on a 49-clip corpus. */
#define HE_MIN_SAMPLE_RATE    32000  /* Fs/2 < 16 kHz below this → core too narrow for SBR */
/* Floor of the HE-AAC range in AUTO mode. Compared LC against HE on the same
 * build, 49 clips, 32 kHz stereo at 8 kbps/channel: HE +0.90 MOS on 48 of 49
 * at 0.7% fewer bits; at 12 kbps/channel (48 kHz) +1.25 on 49 of 49. The old
 * floor of 12000 sent 8 kbps/channel to LC, and was fitted when LC at that
 * rate ran 62% over its target, so the MOS it won was bought with bits. 8000
 * is HE-AAC's design floor and the lowest rate the corpus covers; nothing
 * below it has been measured. */
#define HE_MIN_BITRATE_PER_CH 8000
#define HE_MAX_BITRATE_PER_CH 48000  /* above ceiling LC wins: SBR costs up to 1 MOS on transients */
/* quantqual == totalBitrate/1280 (see faacEncApplyConfig); derived to stay in sync with HE_MAX_BITRATE_PER_CH. */
#define HE_VBR_QUANTQUAL_MAX  (2 * HE_MAX_BITRATE_PER_CH / 1280)

#if (defined WIN32 || defined _WIN32 || defined WIN64 || defined _WIN64) && !defined(PACKAGE_VERSION)
#include "win32_ver.h"
#endif

/* Rate control tuning constants */
#define RC_DAMPING_FACTOR      0.6f   /* Control loop damping */

/* The rate controller's bit account, and how the loop draws on it.
 *
 * This is NOT the AAC bit reservoir, and the old name said otherwise for long
 * enough to mislead. The reservoir is a bitstream property: frame sizes vary
 * and the decoder's input buffer absorbs the variance, bounded by
 * AAC_MAX_BITS_PER_CH and declared through the ADTS buffer_fullness field.
 * That one lives in resFill below faacEncEncode's peak caps: it bounds each
 * frame to what the buffer can take and is what the header declares. Nothing
 * here moves a bit between frames; every raw_data_block is self-contained
 * whatever this balance says.
 *
 * What this is: a clamped integral of the per-frame bit error, whose balance
 * perturbs a psychoacoustic masking-target multiplier. The account metaphor is
 * accurate -- underspend banks, overspend owes, nothing is forgiven -- and
 * `lend` is the share of the balance offered to a frame. It is the soft law
 * that keeps the stream near its rate; the reservoir is the hard bound that
 * keeps it from ever running away, and the two are kept as separate state
 * because this one's bound and gain were tuned on BD-rate and the reservoir's
 * capacity is fixed by the standard.
 *
 * AIM is the fraction of the nominal frame budget the encoder actually banks
 * against, so steady state settles just below target. An average-bitrate mode
 * that is unbiased on average still lands half its clips over budget; aiming a
 * little low makes the average a ceiling instead of a midpoint. The margin is
 * smaller than one step of the quantizer's own quality scale.
 *
 * AMORT spreads the account balance over that many frames before offering it to
 * any one frame, so a large balance is not spent in a single burst and a debt
 * is not repaid by one collapsed frame.
 *
 * AMORT is the strength of the loop's feedback into per-frame quality, and it
 * is the dominant lever in this arm: every candidate for this code sits on one
 * curve trading bitrate accuracy against coding efficiency, and this is what
 * moves along it. Swept on BD-rate against master over the 48 kHz stereo ladder
 * (mean of the HE and LC segments) at ACCOUNT_FRAMES=1:
 *
 *   AMORT      8      16      32      64     256
 *   BD-rate  +0.243  -0.142  -0.420  -0.427  -0.597
 *
 * Monotone, because AMORT -> infinity is `lend` -> 0: the arm buys efficiency by
 * switching off the term that makes it an account, and accuracy walks back to
 * master's 5.33% as it does (4.14 -> 5.19 over that range). So the end of that
 * ladder is not an optimum, it is a surrender, and the value here is chosen
 * with FRAMES for the accuracy the arm exists to deliver. */
#ifndef RC_BALANCE_AIM
#define RC_BALANCE_AIM       0.99f
#endif
#define RC_BALANCE_AMORT     32

/* `lend` reads this account, not the reservoir. Driving it from the
   reservoir's fill instead -- one integrator, the standard's bound -- was
   measured over the full corpus and lost 0.33-0.38% BD-rate on the three LC
   ladders against this: the account's bound is a few frames and the
   reservoir's is fixed by the standard, and the two are not the same kind of
   quantity. Two states, each with its own reason. */

/* How far the account may run in either direction, in frames of budget, and
 * symmetrically: capacity is only two frames, so bounding one side there and the
 * other wider forgives whichever side is tighter and the arm ends up biased
 * toward the side it still remembers.
 *
 * The earlier value of 2 was fitted against mean absolute bitrate error, which
 * reads 3.28 / 3.07 / 3.57 / 3.81 percent at 1 / 2 / 3 / 4 frames and picks 2.
 * That is the wrong objective -- it cannot see coding efficiency at all, and it
 * ranked this whole arm behind alternatives that spend more bits. Judged on
 * BD-rate with AMORT=32, 4 is better on both segments and better on accuracy
 * than the accuracy-fitted value ever was (3.55%/2.46% against master's
 * 5.33%/4.64%). A still wider bound keeps buying efficiency and starts giving
 * accuracy back, which is the same surrender AMORT makes above.
 *
 * A separate per-frame cap on `lend` used to sit alongside this. At two frames
 * it can never bind -- 2/AMORT is already the largest share the balance can
 * offer -- so it was a constant that described the bound rather than limiting
 * anything, and it is gone. */
#define RC_BALANCE_FRAMES      4
/* Time constant of the SBR payload average, as a right shift: 1/16 per frame,
   so ~16 frames (0.37 s at 48 kHz). Long enough to average out the payload's
   frame-to-frame swing, short enough to follow a real change in content. */
#define RC_SBR_EWMA_SHIFT      4
/* Which SBR quantity the controller answers for. 1 = the running average
   (control the core), 0 = this frame's payload (control the total, i.e. the
   behaviour before this change). Compile-time so the losing horn costs nothing
   once it is settled; -DRC_SBR_USE_MEAN=0 must reproduce the old stream
   bit-for-bit, which is how the refactor is checked. */
#ifndef RC_SBR_USE_MEAN
#define RC_SBR_USE_MEAN        1
#endif

/* Bounds on the peak limiter's quality scale factor: the ceiling guarantees
 * each retry makes progress, the floor keeps one outsized frame from
 * collapsing quality to MINQUAL in a single step. */
#define PEAK_BACKOFF_CEILING   0.85f
#define PEAK_BACKOFF_FLOOR     0.10f
#define PEAK_MAX_RETRIES       12

/* Where the bit reservoir opens, as a fraction of its capacity. Full lets the
   first frames run to the standard limit but leaves the whole capacity as
   bits the stream may end up over budget by; empty caps the first frame at the
   mean. Half is the neutral opening. */
#ifndef RC_RESERVOIR_START
#define RC_RESERVOIR_START     0.5f
#endif
/* Let the controller see the reservoir's window for the next frame before it
   codes it. If the frame will be stuffed up to a floor anyway, aim at the
   floor and the bits become signal instead of padding; if it will be capped,
   aim under the cap and the frame fits without a retry. Stuffing on music was
   1-3% of the stream with the controller blind to the floor. */
#ifndef RC_RESERVOIR_AIM
#define RC_RESERVOIR_AIM       1
#endif
/* Hold quality across a run of stuffed frames that do not answer to it. In a
   pause the floor is unreachable at any quality, and raising it anyway winds
   the controller up so that the first frame after the pause busts the cap.
   Only a second stuffed frame with no more bits than the first is held, so a
   quiet passage that does respond keeps its rise. */
#ifndef RC_STUFF_HOLD
#define RC_STUFF_HOLD          1
#endif
/* Whether a frame that would overflow the reservoir is stuffed up to the size
   that does not. A reservoir bounds the buffer on both sides: a frame too big
   stalls the decoder, a frame too small overflows it and the bits are lost to
   the stream. Stuffing is what makes the average bitrate exact on content that
   cannot spend -- pauses, silence -- and it is the difference between a
   reservoir and a mere cap. */
#ifndef RC_RESERVOIR_STUFF
#define RC_RESERVOIR_STUFF     1
#endif

static char *libfaacName = PACKAGE_VERSION;
static char *libCopyright =
  "FAAC - Freeware Advanced Audio Coder (http://faac.sourceforge.net/)\n"
  " Copyright (C) 1999-2001, Menno Bakker\n"
  " Copyright (C) 2002-2017, Krzysztof Nikiel\n"
  " Copyright (C) 2004, Dan Villiom P. Christiansen\n"
  " Copyright (C) 2005-2026, Fabian Greffrath\n"
  " Copyright (C) 2026, Nils Schimmelmann\n";

static unsigned int CalcBandwidth(unsigned long bitRate, unsigned long sampleRate)
{
    const unsigned int nyquist = sampleRate / 2;
    unsigned int bw;

    if (!bitRate) return nyquist;

    if (bitRate <= 16000) {
        /* Segment 1: Telephony (4kHz to 6kHz) */
        bw = 4000 + (bitRate / 8);
    }
    else if (bitRate <= 32000) {
        /* Segment 2: Low-tier (6kHz to 11kHz)
         */
        bw = 6000 + ((bitRate - 16000) * 5 / 16);
    }
    else if (bitRate <= 64000) {
        /* Segment 3: Mid-tier expansion (11kHz to 18.5kHz)
         */
        bw = 11000 + ((bitRate - 32000) * 15 / 64);
    }
    else if (bitRate <= 128000) {
        /* Segment 4: High-fidelity catch-up (18.5kHz to 20kHz) */
        bw = 18500 + ((bitRate - 64000) * 3 / 128);
    }
    else {
        /* Segment 5: Transparency plateau (20kHz+) */
        bw = 20000 + ((bitRate - 128000) / 16);
        if (bw > 20000) bw = 20000;
    }

    /* Safety clamp to Shannon-Nyquist limit */
    return (bw > nyquist) ? nyquist : bw;
}

/* Element-to-channel mapping is fixed for the session once InitElements has
 * run, so cache which channels are LFE here instead of rescanning
 * hEncoder->elements[] for every channel on every frame. */
static void RefreshLfeMap(faacEncStruct *hEncoder)
{
    memset(hEncoder->isLfeChannel, 0, sizeof(hEncoder->isLfeChannel));
    for (int e = 0; e < hEncoder->numElements; e++) {
        if (hEncoder->elements[e].type == ID_LFE)
            hEncoder->isLfeChannel[hEncoder->elements[e].channels[0]] = true;
    }
}

int faacEncGetVersion( char **faac_id_string,
			      				char **faac_copyright_string)
{
  if (faac_id_string)
    *faac_id_string = libfaacName;

  if (faac_copyright_string)
    *faac_copyright_string = libCopyright;

  return FAAC_CFG_VERSION;
}


int faacEncGetDecoderSpecificInfo(faacEncHandle hpEncoder,unsigned char** ppBuffer,unsigned long* pSizeOfDecoderSpecificInfo)
{
    faacEncStruct* hEncoder = (faacEncStruct*)hpEncoder;
    BitStream* pBitStream = NULL;

    if((hEncoder == NULL) || (ppBuffer == NULL) || (pSizeOfDecoderSpecificInfo == NULL)) {
        return -1;
    }

    if(hEncoder->config.mpegVersion == MPEG2){
        return -2; /* not supported */
    }

    if (hEncoder->config.aacObjectType == HE_V1 && hEncoder->sbrContext) {
        return SbrContextGetASC(hEncoder->sbrContext, hEncoder->sampleRateIdx, hEncoder->numChannels, ppBuffer, pSizeOfDecoderSpecificInfo);
    }

    *pSizeOfDecoderSpecificInfo = 2;
    *ppBuffer = (unsigned char *)malloc(2);

    if(*ppBuffer != NULL){
        memset(*ppBuffer,0,*pSizeOfDecoderSpecificInfo);
        pBitStream = OpenBitStream((uint32_t)*pSizeOfDecoderSpecificInfo, *ppBuffer);
        if (!pBitStream) {
            free(*ppBuffer);
            *ppBuffer = NULL;
            return -3;
        }
        PutBit(pBitStream, hEncoder->config.aacObjectType, 5);
        PutBit(pBitStream, hEncoder->sampleRateIdx, 4);
        PutBit(pBitStream, hEncoder->numChannels, 4);
        CloseBitStream(pBitStream);

        return 0;
    } else {
        return -3;
    }
}


/* Configuration worker behind faac_encoder_open(): validates the config,
 * resolves AUTO/HE-AAC, and (re)initializes the encoder for it. Returns 1 on
 * success, 0 on failure. */
int faacEncApplyConfig(faacEncStruct* hEncoder,
                       faacEncConfigurationPtr config)
{
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

    /* If this handle was previously resolved to HE-AAC, restore the native Fs so
     * object-type resolution below always starts from a consistent base (needed
     * when a later call toggles between LC and HE-AAC). */
    SbrContextRestoreRate(hEncoder->sbrContext, &hEncoder->sampleRate, &hEncoder->sampleRateIdx, &hEncoder->srInfo);

    switch( hEncoder->config.inputFormat )
    {
        case INPUT_16BIT:
        case INPUT_24BIT:
        case INPUT_32BIT:
        case INPUT_FLOAT:
            break;
        default:
            return 0;
    }

    /* Only LC, HE-AAC v1, and AUTO (which resolves to one of them) are
     * supported object types. */
    if (hEncoder->config.aacObjectType != LOW &&
        hEncoder->config.aacObjectType != HE_V1 &&
        hEncoder->config.aacObjectType != AUTO)
        return 0;

    /* Check for correct bitrate */
    if (!hEncoder->sampleRate || !hEncoder->numChannels)
        return 0;
    /* Resolve AUTO to LC or HE-AAC. HE-AAC wins for low rates, but only
     * at Fs >= 32 kHz so the Fs/2 core stays >= 16 kHz; below that the
     * narrow-band core + SBR reconstruction collapses. */
    if (hEncoder->config.aacObjectType == AUTO) {
        unsigned long rate_per_ch = config->bitRate;
        int rate_ok;
        if (rate_per_ch > 0) {
            /* Below 44.1 kHz, SBR has less core bandwidth to extend from, so the
             * ceiling ramps down toward 20000 bps/ch at the HE_MIN_SAMPLE_RATE floor. */
            unsigned int max_he_rate = 0;
            if (hEncoder->sampleRate >= 44100) {
                max_he_rate = HE_MAX_BITRATE_PER_CH;
            } else if (hEncoder->sampleRate >= HE_MIN_SAMPLE_RATE) {
                max_he_rate = 20000 + (unsigned int)((hEncoder->sampleRate - 32000) *
                              (HE_MAX_BITRATE_PER_CH - 20000) / (44100 - 32000));
            }
            rate_ok = (rate_per_ch >= HE_MIN_BITRATE_PER_CH && rate_per_ch <= max_he_rate);
        } else {
            rate_ok = (config->quantqual <= HE_VBR_QUANTQUAL_MAX);
        }
        hEncoder->config.aacObjectType = (rate_ok && hEncoder->sampleRate >= HE_MIN_SAMPLE_RATE) ? HE_V1 : LOW;
        config->aacObjectType = hEncoder->config.aacObjectType;
    }

    if (hEncoder->config.aacObjectType == HE_V1 && hEncoder->sampleRate < HE_MIN_SAMPLE_RATE)
        return 0;

    /* HE-AAC: encode the core as AAC-LC; SBR rebuilds the top octave. The core
     * runs dual-rate at Fs/2; the original rate is kept for SBR and the ASC.
     * (Single-rate SBR is not supported: decoders unconditionally reconstruct
     * the SBR band table from 2*core_rate, so a full-Fs core is undecodeable.) */
    if (hEncoder->config.aacObjectType == HE_V1) {
        hEncoder->config.mpegVersion = MPEG4;
        if (!hEncoder->sbrContext)
            hEncoder->sbrContext = SbrContextInit(hEncoder->numChannels);

        if (!hEncoder->sbrContext)
            return 0;

        SbrContextResolveRate(hEncoder->sbrContext, &hEncoder->sampleRate, &hEncoder->sampleRateIdx, &hEncoder->srInfo);
    }

    /* The per-frame ceiling is AAC_MAX_BITS_PER_CH per channel per frame, and
     * a frame is FRAME_LEN samples at the CORE rate -- so this has to follow
     * the HE-AAC rate resolution above, where the core drops to Fs/2 and the
     * ceiling halves with it. MaxBitrate() is already per channel; the old
     * clamp divided it by the channel count again and silently rewrote a
     * stereo -b 320 to 288 at 48 kHz. */
    if (config->bitRate > MaxBitrate(hEncoder->sampleRate))
        config->bitRate = MaxBitrate(hEncoder->sampleRate);

    /* AUTO is the legacy meaning of the two rate fields: a bitrate is ABR, none
       is VBR. The new API rejects the contradictory combinations before this. */
    if (config->rateControl == RATE_AUTO)
        config->rateControl = config->bitRate ? RATE_ABR : RATE_VBR;
    hEncoder->config.rateControl = config->rateControl;

    /* Re-init TNS for new profile */
    TnsInit(hEncoder);

    if (config->bitRate && !config->bandWidth)
    {
        config->bandWidth = CalcBandwidth(config->bitRate, hEncoder->sampleRate);

        if (!config->quantqual)
        {
            /* Scale initial quality seed by sample-rate frame duration factor (44100 / sampleRate)
             * so low sampling rates (e.g. 16 kHz) start at appropriate quality scale factors for
             * fast rate-control convergence on short audio clips. */
            float rateFactor = 44100.0f / (float)hEncoder->sampleRate;
            /* Precise target-bitrate quality seeding curve: maps bitRate to optimal initial quantqual
             * for rapid rate-control convergence without early overshoot or undershoot. */
            float bps = (float)config->bitRate;
            float q_seed;
            if (bps <= 16000.0f) {
                q_seed = 10.0f + 22.0f * (bps / 16000.0f);
            } else if (bps <= 64000.0f) {
                q_seed = 32.0f + 68.0f * ((bps - 16000.0f) / 48000.0f);
            } else {
                q_seed = bps / 640.0f;
            }
            /* Boost initial seed for mono speech streams */
            if (hEncoder->numChannels == 1 && bps >= 32000.0f) q_seed *= 2.5f;
            config->quantqual = q_seed * (float)hEncoder->numChannels * rateFactor;
            if (config->quantqual > DEFQUAL)
                config->quantqual = (config->quantqual - DEFQUAL) * 3.0f + DEFQUAL;
        }
    }

    if (!config->quantqual)
        config->quantqual = DEFQUAL;

    hEncoder->config.bitRate = config->bitRate;

    if (!config->bandWidth)
    {
        config->bandWidth = CalcBandwidth(config->bitRate, hEncoder->sampleRate);
    }

    hEncoder->config.bandWidth = config->bandWidth;

    // check bandwidth
    if (hEncoder->config.bandWidth < 100)
		hEncoder->config.bandWidth = 100;
    if (hEncoder->config.bandWidth > (hEncoder->sampleRate / 2))
		hEncoder->config.bandWidth = hEncoder->sampleRate / 2;

    if (config->quantqual > (unsigned long)maxqual)
        config->quantqual = maxqual;
    if (config->quantqual < MINQUAL)
        config->quantqual = MINQUAL;

    hEncoder->config.quantqual = config->quantqual;

    if (config->mpegVersion == MPEG2)
        config->pnslevel = 0;
    if (config->pnslevel < 0)
        config->pnslevel = 0;
    if (config->pnslevel > 10)
        config->pnslevel = 10;
    hEncoder->aacquantCfg.pnslevel = config->pnslevel;
    /* set quantization quality */
    hEncoder->aacquantCfg.quality = config->quantqual;

    if (hEncoder->config.aacObjectType == HE_V1) {
        SBRContext *sCtx = hEncoder->sbrContext;
        unsigned long sbr_bitrate = hEncoder->config.bitRate ? (hEncoder->config.bitRate * hEncoder->numChannels) : ((unsigned long)hEncoder->config.quantqual * 1280);
        SbrContextUpdateConfig(sCtx, hEncoder->numChannels, sbr_bitrate, &hEncoder->fft_tables);
        /* kx * Fs / (2*64): each QMF band is Fs/(2*SBR_QMF_BANDS_64) Hz wide.
         * Matching core bandwidth to the SBR crossover avoids a gap or overlap. */
        hEncoder->config.bandWidth = SbrContextGetXOverBandwidth(sCtx);
    } else {
        if (hEncoder->sbrContext) {
            SbrContextEnd(hEncoder->sbrContext);
            hEncoder->sbrContext = NULL;
        }
    }

    /* Input FIFO: holds one frame plus up to one full incoming chunk of leftover.
     * HE-AAC frames are 2*FRAME_LEN (the dual-rate core runs at Fs/2), LC is
     * FRAME_LEN. Sizing covers the largest frame the resolved object type could
     * need so toggling SBR across SetConfiguration calls never reallocs. */
    {
        unsigned int cap = 2 * faacFrameSamples(hEncoder);
        unsigned int channel;
        for (channel = 0; channel < hEncoder->numChannels; channel++)
            if (!hEncoder->inputFifo[channel])
            {
                hEncoder->inputFifo[channel] =
                    (float *)AllocMemory(cap * sizeof(float));
                if (!hEncoder->inputFifo[channel]) return 0;
            }
        hEncoder->inputFifoCap  = cap;
        hEncoder->inputFifoFill = 0;
    }

    hEncoder->config.maxBitRate = config->maxBitRate;

    /* Peak-limiter retry scratch: allocated for all encoders to enforce ISO 6144 bits/ch frame ceiling. */
    {
        unsigned int ch;
        for (ch = 0; ch < hEncoder->numChannels; ch++) {
            if (!hEncoder->peakSnap[ch])
                hEncoder->peakSnap[ch] = (int *)AllocMemory(2 * MAX_SCFAC_BANDS * sizeof(int));
            if (!hEncoder->peakSnap[ch])
                return 0;
        }
    }

    CalcBW(&hEncoder->config.bandWidth,
              hEncoder->sampleRate,
              hEncoder->srInfo,
              &hEncoder->aacquantCfg);

    // reset psymodel
    PsyEnd(hEncoder->psyInfo, hEncoder->numChannels);
    PsyInit(&hEncoder->gpsyInfo, hEncoder->psyInfo, hEncoder->numChannels,
			hEncoder->sampleRate);

	/* load channel_map */
	for( i = 0; i < MAX_CHANNELS; i++ )
		hEncoder->config.channel_map[i] = config->channel_map[i];

    InitElements(hEncoder->elements, &hEncoder->numElements, (int)hEncoder->numChannels, hEncoder->config.useLfe);
    RefreshLfeMap(hEncoder);

    /* A signed account is square at zero. The old code opened at half of a
       decoder-buffer-derived capacity, which was right when the balance ran
       [0, cap] and half meant neutral; under a signed account it hands every
       stream a frame of credit it never earned, which `lend` then spends. That
       capacity had no other consumer once the balance gained its own symmetric
       bound, so it is gone rather than left to imply a limit it no longer
       sets. */
    hEncoder->bitBalance = 0;
    /* Negative means "no frame seen yet"; the first frame seeds the average
       rather than dragging it up from zero over the whole time constant. */
    hEncoder->sbrBitsAcc = -1;
    /* The reservoir is sized per frame from the resolved rate and channel
       count, so the first frame opens it; until then there is nothing to
       declare. */
    hEncoder->resMean = 0;
    hEncoder->resCap = 0;
    hEncoder->resFill = -1;
    hEncoder->resMinBits = 0;
    hEncoder->rcPrevWant = 0;

    return 1;
}

#ifdef FAAC_STATS
faacEncStats g_faacStats;
#endif

int faacReservoirAfter(const faacEncStruct *hEncoder, int payloadBits)
{
    /* A frame's share of the rate arrived, the frame left. Below zero the
       decoder would have stalled; the frame cap exists so that does not happen,
       and the clamp is for the frame that cannot be made small enough even at
       MINQUAL. Above capacity a constant-rate channel would carry stuffing that
       this encoder does not emit, so the declared fill saturates instead. */
    int fill = hEncoder->resFill + hEncoder->resMean - payloadBits;
    if (fill < 0)
        fill = 0;
    else if (fill > hEncoder->resCap)
        fill = hEncoder->resCap;
    return fill;
}

faacEncHandle faacEncOpen(unsigned long sampleRate,
                                  unsigned int numChannels,
                                  unsigned long *inputSamples,
                                  unsigned long *maxOutputBytes)
{
#ifdef FAAC_STATS
    memset(&g_faacStats, 0, sizeof(faacEncStats));
    g_faacStats.minBalanceRatio = 100.0f;
#endif
    unsigned int channel;
    faacEncStruct* hEncoder;

    if (numChannels < 1 || numChannels > MAX_CHANNELS)
	return NULL;

    *inputSamples = FRAME_LEN*numChannels;
    *maxOutputBytes = ADTS_FRAMESIZE;

    hEncoder = (faacEncStruct*)AllocMemory(sizeof(faacEncStruct));
    if (!hEncoder) return NULL;
    SetMemory(hEncoder, 0, sizeof(faacEncStruct));

    hEncoder->numChannels = numChannels;
    hEncoder->sampleRate = sampleRate;
    hEncoder->sampleRateIdx = GetSRIndex(sampleRate);

    /* Initialize variables to default values */
    hEncoder->frameNum = 0;
    hEncoder->flushFrame = 0;

    /* Default configuration */
    hEncoder->config.mpegVersion = MPEG4;
    hEncoder->config.aacObjectType = LOW;
    hEncoder->config.jointmode = JOINT_MIXED;
    hEncoder->config.pnslevel = 4;
    hEncoder->config.useLfe = 1;
    hEncoder->config.useTns = 0;
    hEncoder->config.bitRate = 64000;
    hEncoder->config.bandWidth = CalcBandwidth(hEncoder->config.bitRate, sampleRate);
    hEncoder->config.quantqual = 0;
    hEncoder->config.shortctl = SHORTCTL_NORMAL;

	/* default channel map is straight-through */
	for( channel = 0; channel < MAX_CHANNELS; channel++ )
		hEncoder->config.channel_map[channel] = channel;

    hEncoder->config.outputFormat = ADTS_STREAM;

    /*
        be compatible with software which assumes 24bit in 32bit PCM
    */
    hEncoder->config.inputFormat = INPUT_32BIT;

    /* find correct sampling rate depending parameters */
    hEncoder->srInfo = &srInfo[hEncoder->sampleRateIdx];

    for (channel = 0; channel < numChannels; channel++)
	{
        int buf;
        hEncoder->coderInfo[channel].prev_window_shape = SINE_WINDOW;
        hEncoder->coderInfo[channel].window_shape = SINE_WINDOW;
        hEncoder->coderInfo[channel].block_type = ONLY_LONG_WINDOW;
        hEncoder->coderInfo[channel].groups.n = 1;
        hEncoder->coderInfo[channel].groups.len[0] = 1;

        for (buf = 0; buf < 4; buf++) {
            hEncoder->audioFIFO[channel][buf] = (float*)AllocMemory(FRAME_LEN*sizeof(float));
            if (!hEncoder->audioFIFO[channel][buf])
            {
                faacEncClose(hEncoder);
                return NULL;
            }
            memset(hEncoder->audioFIFO[channel][buf], 0, FRAME_LEN*sizeof(float));
        }
    }

    /* Initialize coder functions */
    InitElements(hEncoder->elements, &hEncoder->numElements, (int)hEncoder->numChannels, (bool)hEncoder->config.useLfe);
    RefreshLfeMap(hEncoder);

	fft_initialize( &hEncoder->fft_tables );

	PsyInit(&hEncoder->gpsyInfo, hEncoder->psyInfo, hEncoder->numChannels,
        hEncoder->sampleRate);

    FilterBankInit(hEncoder);

    TnsInit(hEncoder);

    QuantizeInit();

    /* Return handle */
    return hEncoder;
}


/* Append the caller's (interleaved) input to the per-channel input FIFO,
 * de-interleaving and converting to float once here so the rest of the
 * encoder is agnostic to the input format. samplesInput may be any count that
 * fits the FIFO; returns -1 on overflow or an invalid format. */
static int appendInputFifo(faacEncStruct *hEncoder, int32_t *inputBuffer,
                           unsigned int samplesInput)
{
    unsigned int numChannels = hEncoder->numChannels;
    unsigned int spch = samplesInput / numChannels;
    unsigned int channel, i;

    if (spch == 0) return 0;
    if (hEncoder->inputFifoFill + spch > hEncoder->inputFifoCap) return -1;

    for (channel = 0; channel < numChannels; channel++) {
        float *dst = hEncoder->inputFifo[channel] + hEncoder->inputFifoFill;
        switch (hEncoder->config.inputFormat) {
            case INPUT_16BIT: {
                short *src = (short *)inputBuffer + hEncoder->config.channel_map[channel];
                for (i = 0; i < spch; i++) { dst[i] = (float)*src; src += numChannels; }
                break;
            }
            case INPUT_24BIT: {
                const uint8_t *src_base = (const uint8_t *)inputBuffer;
                for (i = 0; i < spch; i++) {
                    const uint8_t *src = src_base + (i * numChannels + hEncoder->config.channel_map[channel]) * 3;
#if defined(WORDS_BIGENDIAN) && WORDS_BIGENDIAN
                    int32_t s = ((int32_t)src[0] << 16) | ((int32_t)src[1] << 8) | (int32_t)src[2];
#else
                    int32_t s = (int32_t)src[0] | ((int32_t)src[1] << 8) | ((int32_t)src[2] << 16);
#endif
                    if (s & 0x800000) s |= (int32_t)0xff000000;
                    dst[i] = (1.0f / 256.0f) * (float)s;
                }
                break;
            }
            case INPUT_32BIT: {
                int32_t *src = (int32_t *)inputBuffer + hEncoder->config.channel_map[channel];
                for (i = 0; i < spch; i++) { dst[i] = (1.0f/256) * (float)*src; src += numChannels; }
                break;
            }
            case INPUT_FLOAT: {
                float *src = (float *)inputBuffer + hEncoder->config.channel_map[channel];
                for (i = 0; i < spch; i++) { dst[i] = (float)*src; src += numChannels; }
                break;
            }
            default: return -1;
        }
    }
    hEncoder->inputFifoFill += spch;
    return 0;
}

/* Drop n samples/channel from the front of the FIFO, shifting the leftover down. */
static void consumeInputFifo(faacEncStruct *hEncoder, unsigned int n)
{
    unsigned int numChannels = hEncoder->numChannels;
    unsigned int channel, rem;

    if (n > hEncoder->inputFifoFill) n = hEncoder->inputFifoFill;
    rem = hEncoder->inputFifoFill - n;
    if (rem)
        for (channel = 0; channel < numChannels; channel++)
            memmove(hEncoder->inputFifo[channel], hEncoder->inputFifo[channel] + n, rem * sizeof(float));
    hEncoder->inputFifoFill = rem;
}

int faacEncClose(faacEncHandle hpEncoder)
{
    faacEncStruct* hEncoder = (faacEncStruct*)hpEncoder;
    unsigned int channel;

    if (!hEncoder) return 0;

#ifdef FAAC_STATS
    if (g_faacStats.totalFrames > 0)
    {
        double qavg = g_faacStats.totalQuality / g_faacStats.totalFrames;
        double tr = 100.0 * g_faacStats.transientFrames / g_faacStats.totalFrames;
        double tns = g_faacStats.longBlocks > 0 ? 100.0 * g_faacStats.longBlocksTNS / g_faacStats.longBlocks : 0.0;
        double ms = g_faacStats.totalBands > 0 ? 100.0 * g_faacStats.msBands / g_faacStats.totalBands : 0.0;
        double is = g_faacStats.totalBands > 0 ? 100.0 * g_faacStats.isBands / g_faacStats.totalBands : 0.0;
        double pns = g_faacStats.totalBands > 0 ? 100.0 * g_faacStats.pnsBands / g_faacStats.totalBands : 0.0;
        double att_avg = g_faacStats.attackCount > 0 ? g_faacStats.totalAttack / g_faacStats.attackCount : 0.0;
        float att_max = g_faacStats.maxAttack;

        fprintf(stderr, "\n--- Encoder Diagnostics ---\n");
        fprintf(stderr, " Quality             : Qavg    = %6.2f\n", qavg);

        if (g_faacStats.sbrFrames > 0)
        {
            double sbr_tr = 100.0 * g_faacStats.sbrTransientFrames / g_faacStats.sbrFrames;
            fprintf(stderr, " Transients & Grid   : Core Tr = %5.1f%% (%u/%u) | SBR Grid = %5.1f%% Var | Attack = %.1fx avg, %.1fx max\n",
                    tr, g_faacStats.transientFrames, g_faacStats.totalFrames, sbr_tr, att_avg, att_max);
        }
        else
        {
            fprintf(stderr, " Transients & Grid   : Core Tr = %5.1f%% (%u/%u) | Attack = %.1fx avg, %.1fx max\n",
                    tr, g_faacStats.transientFrames, g_faacStats.totalFrames, att_avg, att_max);
        }

        if (g_faacStats.shortChannels > 0)
        {
            double grp_avg = (double)g_faacStats.shortGroupSum / g_faacStats.shortChannels;
            double split = 100.0 * g_faacStats.shortSplitChannels / g_faacStats.shortChannels;
            fprintf(stderr, " Short Grouping      : Groups  = %5.2f avg/ch | Split = %5.1f%% of %u short ch\n",
                    grp_avg, split, g_faacStats.shortChannels);
        }

        double peak_retry_pct = 100.0 * g_faacStats.peakRetryFrames / g_faacStats.totalFrames;

        if (g_faacStats.sbrFrames > 0)
        {
            double sbr_invf = g_faacStats.sbrInvfCount > 0 ? (double)g_faacStats.sbrInvfSum / g_faacStats.sbrInvfCount : 0.0;
            fprintf(stderr, " Tool Allocation     : M/S     = %5.1f%% | I/S = %5.1f%% | PNS = %5.1f%% | TNS = %5.1f%% | INVF = %.2f\n",
                    ms, is, pns, tns, sbr_invf);
        }
        else
        {
            fprintf(stderr, " Tool Allocation     : M/S     = %5.1f%% | I/S = %5.1f%% | PNS = %5.1f%% | TNS = %5.1f%%\n",
                    ms, is, pns, tns);
        }
        if (g_faacStats.balanceFrames > 0 || g_faacStats.peakRetryFrames > 0)
        {
            if (g_faacStats.balanceFrames > 0 && g_faacStats.peakRetryFrames > 0)
            {
                double res_fill = g_faacStats.totalBalanceRatio / g_faacStats.balanceFrames;
                fprintf(stderr, " Rate Control & Cap  : Bit Balance    = %5.1f%% (min %5.1f%%, max %5.1f%%) | Peak Limit Retries = %5.1f%% (%u/%u)\n",
                        res_fill, g_faacStats.minBalanceRatio, g_faacStats.maxBalanceRatio,
                        peak_retry_pct, g_faacStats.peakRetryFrames, g_faacStats.totalFrames);
            }
            else if (g_faacStats.balanceFrames > 0)
            {
                double res_fill = g_faacStats.totalBalanceRatio / g_faacStats.balanceFrames;
                fprintf(stderr, " Rate Control & Cap  : Bit Balance    = %5.1f%% (min %5.1f%%, max %5.1f%%)\n",
                        res_fill, g_faacStats.minBalanceRatio, g_faacStats.maxBalanceRatio);
            }
            else
            {
                fprintf(stderr, " Rate Control & Cap  : Peak Limit Retries = %5.1f%% (%u/%u)\n",
                        peak_retry_pct, g_faacStats.peakRetryFrames, g_faacStats.totalFrames);
            }
        }
        if (g_faacStats.resFrames > 0)
        {
            fprintf(stderr, " Bit Reservoir       : Fill = %5.1f%% (min %5.1f%%, max %5.1f%%) | Bound Retries = %5.1f%% (%u/%u) | Underflows = %u\n",
                    g_faacStats.totalResFill / g_faacStats.resFrames,
                    g_faacStats.minResFill, g_faacStats.maxResFill,
                    100.0 * g_faacStats.resBoundRetryFrames / g_faacStats.resFrames,
                    g_faacStats.resBoundRetryFrames, g_faacStats.resFrames,
                    g_faacStats.resUnderflowFrames);
            fprintf(stderr, " Reservoir Stuffing  : Frames = %5.1f%% (%u/%u) | Bits = %5.2f%% of stream\n",
                    100.0 * g_faacStats.resStuffFrames / g_faacStats.resFrames,
                    g_faacStats.resStuffFrames, g_faacStats.resFrames,
                    g_faacStats.resTotalBits > 0 ? 100.0 * g_faacStats.resStuffBits / g_faacStats.resTotalBits : 0.0);
        }
        if (g_faacStats.balanceFrames > 0)
        {
            unsigned int rcf = g_faacStats.balanceFrames;
            fprintf(stderr, " Bit Error           : mean over = %7.1f bits (%u fr) | mean under = %7.1f bits (%u fr) | net = %+6.2f%% of target\n",
                    g_faacStats.overshootFrames ? g_faacStats.sumOverBits / g_faacStats.overshootFrames : 0.0,
                    g_faacStats.overshootFrames,
                    g_faacStats.underFrames ? g_faacStats.sumUnderBits / g_faacStats.underFrames : 0.0,
                    g_faacStats.underFrames,
                    g_faacStats.sumDesBits > 0 ? 100.0 * (g_faacStats.sumOverBits - g_faacStats.sumUnderBits) / g_faacStats.sumDesBits : 0.0);
            fprintf(stderr, " Quality Clamp       : Overshoot = %5.1f%% (%u/%u) | at MINQUAL = %5.1f%% | overshoot AND floored = %5.1f%% | at MAXQUAL = %5.1f%%\n",
                    100.0 * g_faacStats.overshootFrames / rcf, g_faacStats.overshootFrames, rcf,
                    100.0 * g_faacStats.minqualFrames / rcf,
                    100.0 * g_faacStats.minqualOvershootFrames / rcf,
                    100.0 * g_faacStats.maxqualFrames / rcf);
        }
        fprintf(stderr, "---------------------------\n");
    }
#endif

    PsyEnd(hEncoder->psyInfo, hEncoder->numChannels);
    FilterBankEnd(hEncoder);
    fft_terminate(&hEncoder->fft_tables);

    for (channel = 0; channel < hEncoder->numChannels; channel++)
	{
        int buf;
        for (buf = 0; buf < 4; buf++) {
            if (hEncoder->audioFIFO[channel][buf])
                FreeMemory(hEncoder->audioFIFO[channel][buf]);
        }
		if (hEncoder->inputFifo[channel])
			FreeMemory (hEncoder->inputFifo[channel]);
        if (hEncoder->peakSnap[channel])
            FreeMemory(hEncoder->peakSnap[channel]);
    }

    if (hEncoder->ascCache) free(hEncoder->ascCache);

    if (hEncoder->sbrContext) {
        SbrContextEnd(hEncoder->sbrContext);
        hEncoder->sbrContext = NULL;
    }

    FreeMemory(hEncoder);

    return 0;
}

/* HE-AAC per-frame front end: take one assembled full-rate frame from the FIFO
 * front (realPerCh real samples/ch, the rest silence-padded), run SBR analysis
 * on it, then 2:1 downsample to produce the AAC-LC core signal. The FIFO is not
 * consumed here; the caller drops the frame after the core has read heHalfRate.
 * Cold path, kept out of the LC fast path. */
#if defined(__GNUC__)
__attribute__((cold, noinline))
#endif
static void doHEAACFrame(faacEncStruct *hEncoder, unsigned int realPerCh,
                         float *heHalfRate[MAX_CHANNELS])
{
    SbrContextProcessFrame(hEncoder->sbrContext, hEncoder->numChannels, (int)realPerCh, hEncoder->inputFifo, heHalfRate);
}

/* Admission gate: TNS shapes noise along the temporal envelope, so a window
 * with no envelope discontinuity has nothing for it to do, but the LPC gate
 * only discovers that after normalization, autocorrelation and Levinson-Durbin
 * have run. Screening on the envelope first skips that work for frames headed
 * for rejection anyway.
 *
 * Scaled to PsyGetAttack's statistic (largest relative energy jump between
 * adjacent sub-blocks). Not portable to a different sub-block count/size --
 * the same transient reads as a smaller jump with fewer, longer sub-blocks. */
#define TNS_ATTACK_MIN 0.5f

int faacEncEncode(faacEncHandle hpEncoder,
                          int32_t *inputBuffer,
                          unsigned int samplesInput,
                          unsigned char *outputBuffer,
                          unsigned int bufferSize
                          )
{
    faacEncStruct* hEncoder = (faacEncStruct*)hpEncoder;
    unsigned int channel;
    int sb, frameBytes;
    unsigned int offset;
    BitStream *bitStream;

    CoderInfo *coderInfo = hEncoder->coderInfo;
    unsigned int numChannels = hEncoder->numChannels;
    unsigned int useTns = hEncoder->config.useTns;
    unsigned int jointmode = hEncoder->config.jointmode;
    unsigned int shortctl = hEncoder->config.shortctl;
    int maxqual = hEncoder->config.outputFormat ? MAXQUALADTS : MAXQUAL;

    /* The input FIFO decouples the caller's chunk size from the encoder frame
     * size: append whatever we were handed, then emit at most one frame. A frame
     * is mult*FRAME_LEN samples/channel (mult==2 for HE-AAC, whose dual-rate core
     * runs at Fs/2; 1 for LC). While fewer than a full frame is
     * buffered we just return 0 without touching any per-frame state, so the
     * encoder behaves identically regardless of the caller's chunk size. */
    unsigned int frameSamplesPerCh = faacFrameSamples(hEncoder);
    int flushing = (samplesInput == 0 || inputBuffer == NULL);

    if (samplesInput > 0 && inputBuffer != NULL)
    {
        if (appendInputFifo(hEncoder, inputBuffer, samplesInput) < 0)
            return -1;
    }

    /* A 0-byte return is ambiguous during end-of-stream flushing. While flushing,
     * absorb no-output pipeline priming ticks internally until an encoded frame
     * is produced or core lookahead delay is fully drained. */
    do {
        int realPerCh;          /* real (non-padded) input samples/ch in this frame */
        if (hEncoder->inputFifoFill >= frameSamplesPerCh)
            realPerCh = (int)frameSamplesPerCh;           /* full frame ready */
        else if (flushing && hEncoder->inputFifoFill > 0)
            realPerCh = (int)hEncoder->inputFifoFill;     /* final partial frame */
        else if (flushing)
            realPerCh = 0;                                /* drain core lookahead */
        else
            return 0;                                     /* accumulating */

        /* Increase frame number */
        hEncoder->frameNum++;

        /* A pure (FIFO-empty) flush frame pushes silence to drain the core's
         * algorithmic delay; a final partial frame still carries real samples and is
         * counted like a data frame, matching the pre-FIFO behaviour. */
        if (realPerCh == 0)
            hEncoder->flushFrame++;

        /* SBR's coded-payload ring (frameFIFO) trails the core FIFO by one
         * extra tick, so HE-AAC needs one more flush tick than LC to drain. */
        unsigned int flushBudget = (hEncoder->config.aacObjectType == HE_V1) ?
            SBR_FRAME_FIFO : (LOOKAHEAD_DEPTH + 1);
        if (hEncoder->flushFrame > flushBudget)
            return 0;

        /* HE-AAC: run SBR + downsample first; the core then encodes heHalfRate.
         * Flush frames (realPerCh == 0) included -- the SBR payload runs
         * SBR_FRAME_FIFO-1 frames behind, so the pipeline has to keep ticking
         * through the drain or the tail access units re-emit stale envelopes. */
        float *heHalfRate[MAX_CHANNELS] = {0};
        if (hEncoder->config.aacObjectType == HE_V1 && SbrContextIsPresent(hEncoder->sbrContext))
            doHEAACFrame(hEncoder, (unsigned int)realPerCh, heHalfRate);

        /* Update current sample buffers */
        for (channel = 0; channel < numChannels; channel++)
        {
            float *tmp = hEncoder->audioFIFO[channel][FIFO_PAST];
            hEncoder->audioFIFO[channel][FIFO_PAST]   = hEncoder->audioFIFO[channel][FIFO_CURR];
            hEncoder->audioFIFO[channel][FIFO_CURR]   = hEncoder->audioFIFO[channel][FIFO_AHEAD1];
            hEncoder->audioFIFO[channel][FIFO_AHEAD1]  = hEncoder->audioFIFO[channel][FIFO_AHEAD2];
            hEncoder->audioFIFO[channel][FIFO_AHEAD2] = tmp;

            if (realPerCh == 0)
            {
                /* start flushing*/
                memset(hEncoder->audioFIFO[channel][FIFO_AHEAD2], 0, FRAME_LEN * sizeof(float));
            }
            else if (hEncoder->config.aacObjectType == HE_V1 && heHalfRate[channel])
            {
                /* core feeds on the SBR-downsampled signal, not the raw input */
                memcpy(hEncoder->audioFIFO[channel][FIFO_AHEAD2], heHalfRate[channel], FRAME_LEN * sizeof(float));
            }
            else
            {
                /* LC: take one frame from the FIFO front (already float),
                 * silence-padding a short final frame. */
                unsigned int spc = ((unsigned int)realPerCh < FRAME_LEN) ? (unsigned int)realPerCh : FRAME_LEN;
                memcpy(hEncoder->audioFIFO[channel][FIFO_AHEAD2], hEncoder->inputFifo[channel], spc * sizeof(float));
                if (spc < FRAME_LEN)
                    memset(hEncoder->audioFIFO[channel][FIFO_AHEAD2] + spc, 0, (FRAME_LEN - spc) * sizeof(float));
            }

            /* LFE's block_type is always forced to ONLY_LONG_WINDOW in PsyCalculate,
             * so the transient analysis below would be discarded -- skip it. */
            if (!hEncoder->isLfeChannel[channel])
            {
                /* Shared detector replacement on HE: skip half-rate PsyBufferUpdate. */
                if (hEncoder->config.aacObjectType != HE_V1 || !SbrContextIsAnalysisValid(hEncoder->sbrContext))
                {
                    PsyBufferUpdate(&hEncoder->gpsyInfo, &hEncoder->psyInfo[channel],
                        hEncoder->audioFIFO[channel][FIFO_AHEAD1],
                        hEncoder->audioFIFO[channel][FIFO_AHEAD2]);
                }
            }
        }

        /* Drop the consumed frame from the FIFO front (both the LC copy and the
         * HE doHEAACFrame read the leading frameSamplesPerCh samples). */
        if (realPerCh > 0)
            consumeInputFifo(hEncoder, frameSamplesPerCh);

        if (hEncoder->frameNum > LOOKAHEAD_DEPTH)
            break;
    } while (flushing);

    if (!flushing && hEncoder->frameNum <= LOOKAHEAD_DEPTH) /* Still filling up the buffers */
        return 0;

    /* Psychoacoustics */
    /* Shared detector replacement on HE: skip half-rate PsyCalculate. */
    if (hEncoder->config.aacObjectType != HE_V1 || !SbrContextIsAnalysisValid(hEncoder->sbrContext))
        PsyCalculate(hEncoder->elements, hEncoder->numElements, hEncoder->psyInfo, numChannels);

    BlockSwitch(hEncoder, coderInfo, hEncoder->psyInfo, numChannels);

#ifdef FAAC_STATS
    g_faacStats.totalFrames++;
    if (coderInfo[0].block_type == ONLY_SHORT_WINDOW || coderInfo[0].block_type == LONG_SHORT_WINDOW)
    {
        g_faacStats.transientFrames++;
    }
#endif

    /* force block type */
    if (shortctl == SHORTCTL_NOSHORT)
    {
		for (channel = 0; channel < numChannels; channel++)
		{
			coderInfo[channel].block_type = ONLY_LONG_WINDOW;
		}
    }
    else if ((hEncoder->frameNum <= (LOOKAHEAD_DEPTH + 1)) || (shortctl == SHORTCTL_NOLONG))
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
            hEncoder->audioFIFO[channel][FIFO_PAST],
            hEncoder->audioFIFO[channel][FIFO_CURR],
            hEncoder->freqBuff[channel]);
    }

    for (channel = 0; channel < numChannels; channel++) {
        if (coderInfo[channel].block_type == ONLY_SHORT_WINDOW) {
            coderInfo[channel].sfbn = hEncoder->aacquantCfg.max_cbs;

            offset = 0;
            for (sb = 0; sb < coderInfo[channel].sfbn; sb++) {
                coderInfo[channel].sfb_offset[sb] = offset;
                offset += hEncoder->srInfo->cb_width_short[sb];
            }
            coderInfo[channel].sfb_offset[sb] = offset;
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

    /* Funnelled through one call site so BlocGroup stays a single inlined copy. */
    for (int e = 0; e < hEncoder->numElements; e++)
    {
        AACElement *el = &hEncoder->elements[e];
        int l = el->channels[0];
        int r = (el->type == ID_CPE) ? el->channels[1] : -1;
        CoderInfo *a = NULL, *b = NULL;
        float *xa = NULL, *xb = NULL;

        if (coderInfo[l].block_type == ONLY_SHORT_WINDOW)
        {
            a = &coderInfo[l];
            xa = hEncoder->freqBuff[l];
            if (r >= 0 && coderInfo[r].block_type == ONLY_SHORT_WINDOW)
            {
                b = &coderInfo[r];
                xb = hEncoder->freqBuff[r];
            }
        }
        else if (r >= 0 && coderInfo[r].block_type == ONLY_SHORT_WINDOW)
        {
            a = &coderInfo[r];
            xa = hEncoder->freqBuff[r];
        }

        if (a)
        {
            BlocGroup(a, xa, b, xb, &hEncoder->aacquantCfg);
#ifdef FAAC_STATS
            /* Everything downstream scales with groups.n * sfbn, so this one
             * number covers both the throughput and the bitrate axis. */
            {
                unsigned int nch = b ? 2 : 1;
                g_faacStats.shortChannels += nch;
                g_faacStats.shortGroupSum += (unsigned long)a->groups.n * nch;
                if (a->groups.n > 1)
                    g_faacStats.shortSplitChannels += nch;
            }
#endif
        }
    }

    /* Perform TNS analysis and filtering */
    for (channel = 0; channel < numChannels; channel++) {
        if (!hEncoder->isLfeChannel[channel] && useTns) {
            float attack = PsyGetAttack(&hEncoder->psyInfo[channel]);

#ifdef FAAC_STATS
            if (attack > 0.0f && isfinite(attack)) {
                g_faacStats.totalAttack += attack;
                if (attack > g_faacStats.maxAttack) {
                    g_faacStats.maxAttack = attack;
                }
                g_faacStats.attackCount++;
            }
            if (coderInfo[channel].block_type != ONLY_SHORT_WINDOW) {
                g_faacStats.longBlocks++;
            }
#endif

            /* No envelope available (HE-AAC skips PsyBufferUpdate) means no
               basis to reject on, so admit and let the LPC gates decide. */
            if (attack > 0.0f && attack < TNS_ATTACK_MIN) {
                coderInfo[channel].tnsInfo.tnsDataPresent = 0;
                continue;
            }
            TnsEncode(&(coderInfo[channel].tnsInfo),
                      coderInfo[channel].sfbn,
                      coderInfo[channel].block_type,
                      coderInfo[channel].sfb_offset,
                      hEncoder->freqBuff[channel]);
        } else {
            coderInfo[channel].tnsInfo.tnsDataPresent = 0;      /* TNS not used for LFE */
        }
    }

    for (int e = 0; e < hEncoder->numElements; e++) {
      // reduce LFE bandwidth
		if (hEncoder->elements[e].type == ID_LFE)
		{
                    coderInfo[hEncoder->elements[e].channels[0]].sfbn = 3;
		}
	}

    /* Clear each channel's section state before AACstereo pre-loads intensity
     * bands and BlocQuant resolves the rest. */
    for (channel = 0; channel < numChannels; channel++)
        ResetCoderSections(&coderInfo[channel]);

    AACstereo(coderInfo, hEncoder->elements, hEncoder->numElements, hEncoder->freqBuff,
              (float)hEncoder->aacquantCfg.quality/DEFQUAL, jointmode, hEncoder->sampleRate,
              hEncoder->config.bandWidth);

    /* AACstereo has already consumed freqBuff in place and BlocQuant
     * accumulates into sf[] while reading book[], so a retry can re-run
     * neither. Snapshot what they produce -- book, sf, and the sfbn the CPE fix
     * below rewrites -- so a retry restarts from identical state. */
    unsigned long long peakBits = 0;
    float baseQuality = hEncoder->aacquantCfg.quality;
    int sfbnSnap[MAX_CHANNELS];
    int attempt;
    /* Every cap below is on the raw_data_block; the ADTS header is transport. */
    int hdrBytes = (hEncoder->config.outputFormat == 1) ? ADTS_HEADER_SIZE : 0;
    int payloadBits = 0;
#ifdef FAAC_STATS
    int resBound = 0;
#endif

    /* ISO/IEC 14496-3 standard frame limit: 6144 bits per channel */
    peakBits = (unsigned long long)numChannels * AAC_MAX_BITS_PER_CH;

    /* If output format is ADTS (outputFormat == 1), respect the 13-bit ADTS
     * container frame length limit (ADTS_MAX_FRAME_SIZE = 8191 bytes = 65528 bits). */
    if (hEncoder->config.outputFormat == 1)
    {
        unsigned long long adtsPeakBits = (unsigned long long)(ADTS_MAX_FRAME_SIZE - ADTS_HEADER_SIZE) * 8;
        if (adtsPeakBits < peakBits)
            peakBits = adtsPeakBits;
    }

    if (hEncoder->config.maxBitRate)
    {
        /* maxBitRate is whole-stream, so no channel factor here. For HE-AAC
         * sampleRate is the halved core rate, which is what makes FRAME_LEN
         * cover the right span of output samples. */
        unsigned long long userPeakBits = (unsigned long long)hEncoder->config.maxBitRate
            * FRAME_LEN / hEncoder->sampleRate;
        if (userPeakBits < peakBits)
            peakBits = userPeakBits;
    }

    /* The bit reservoir, CBR only. The decoder's input buffer holds
       AAC_MAX_BITS_PER_CH per channel; it is refilled at the mean frame rate
       and drained one frame at a time, so what this frame may spend is the
       mean plus whatever the buffer holds. The standard limit above is that
       same cap with the buffer full; --cap-rate and the ADTS frame length keep
       their own terms, and min() lets whichever is tightest bind. Enforcement
       is the retry loop below, which was already there for the other caps -- a
       frame that fits costs nothing extra. ABR and VBR leave resMean at 0:
       no cap term, no stuffing floor, buffer_fullness stays the VBR sentinel.
       Measured over the full corpus, the reservoir costs ABR 0.5-0.7% BD-rate
       on music (quiet passages stuffed) and caps the onsets speech spends 4x
       the mean on; what it buys -- an exact rate, a conformant constant-rate
       stream -- only a constant-rate channel or a matched-rate comparison
       needs, so it is asked for rather than imposed. */
    if (hEncoder->config.rateControl == RATE_CBR)
    {
        int mean = numChannels * (hEncoder->config.bitRate * FRAME_LEN)
            / hEncoder->sampleRate;
        unsigned long long avail;

        hEncoder->resMean = mean;
        hEncoder->resCap = (int)numChannels * AAC_MAX_BITS_PER_CH - mean;
        if (hEncoder->resCap < 0)
            hEncoder->resCap = 0;
        if (hEncoder->resFill < 0)
            hEncoder->resFill = (int)(hEncoder->resCap * RC_RESERVOIR_START);

        avail = (unsigned long long)(mean + hEncoder->resFill);
        if (avail < peakBits)
        {
            peakBits = avail;
#ifdef FAAC_STATS
            resBound = 1;
#endif
        }
        /* The floor is at most the cap less what byte-granular fill and the
           frame's byte alignment can add, so stuffing can never push a frame
           over the cap when the two meet -- at the ceiling bitrate the
           reservoir has no capacity and they meet on every frame. */
        hEncoder->resMinBits = RC_RESERVOIR_STUFF
            ? mean + hEncoder->resFill - hEncoder->resCap : 0;
        if (hEncoder->resMinBits > (int)avail - 16)
            hEncoder->resMinBits = (int)avail - 16;
        if (hEncoder->resMinBits < 0)
            hEncoder->resMinBits = 0;
    }

    for (channel = 0; channel < numChannels; channel++) {
        memcpy(hEncoder->peakSnap[channel], coderInfo[channel].book,
               MAX_SCFAC_BANDS * sizeof(int));
        memcpy(hEncoder->peakSnap[channel] + MAX_SCFAC_BANDS, coderInfo[channel].sf,
               MAX_SCFAC_BANDS * sizeof(int));
        sfbnSnap[channel] = coderInfo[channel].sfbn;
    }

    /* Retry while the frame busts peakBits. The search is bounded, not
     * exact-fit: an exact fit can fail to terminate on pathological input. */
    for (attempt = 0; attempt <= PEAK_MAX_RETRIES; attempt++)
    {
        for (channel = 0; channel < numChannels; channel++) {
            BlocQuant(&coderInfo[channel], hEncoder->freqBuff[channel],
                      &(hEncoder->aacquantCfg));
        }

        // fix max_sfb in CPE mode
        for (int e = 0; e < hEncoder->numElements; e++)
        {
            if (hEncoder->elements[e].type == ID_CPE)
            {
                CoderInfo *cil, *cir;

                cil = &coderInfo[hEncoder->elements[e].channels[0]];
                cir = &coderInfo[hEncoder->elements[e].channels[1]];

                cil->sfbn = cir->sfbn = max(cil->sfbn, cir->sfbn);
            }
        }

        /* Write the AAC bitstream; the write doubles as the size probe. */
        bitStream = OpenBitStream(bufferSize, outputBuffer);
        if (!bitStream)
            return -1;

        if (WriteBitstream(hEncoder, coderInfo, hEncoder->elements, hEncoder->numElements, bitStream) < 0)
            return -1;

        /* Close the bitstream and return the number of bytes written */
        frameBytes = CloseBitStream(bitStream);
        payloadBits = (frameBytes - hdrBytes) * 8;

        if (!peakBits || (unsigned long long)payloadBits <= peakBits
            || hEncoder->aacquantCfg.quality <= MINQUAL)
            break;

        /* Aim at the budget rather than stepping down by a fixed factor: rate
         * control can park quality anywhere up to MAXQUAL (5000), and a fixed
         * halving needs ~9 passes to cross that to MINQUAL (10), more than any
         * sane retry budget. Frame bits grow sub-linearly with quality, so
         * scaling by the bit ratio undershoots the budget and converges in a
         * pass or two. */
        float scale = (float)peakBits / (float)payloadBits;
        if (scale > PEAK_BACKOFF_CEILING) scale = PEAK_BACKOFF_CEILING;
        if (scale < PEAK_BACKOFF_FLOOR)   scale = PEAK_BACKOFF_FLOOR;
        hEncoder->aacquantCfg.quality *= scale;
        if (hEncoder->aacquantCfg.quality < MINQUAL)
            hEncoder->aacquantCfg.quality = MINQUAL;

        for (channel = 0; channel < numChannels; channel++) {
            memcpy(coderInfo[channel].book, hEncoder->peakSnap[channel],
                   MAX_SCFAC_BANDS * sizeof(int));
            memcpy(coderInfo[channel].sf, hEncoder->peakSnap[channel] + MAX_SCFAC_BANDS,
                   MAX_SCFAC_BANDS * sizeof(int));
            coderInfo[channel].sfbn = sfbnSnap[channel];
        }
    }

    /* The caps are per frame, so the backoff must not outlive the frame: left
     * sticky, one hard frame drags the rest of the stream down -- measured at
     * 2% BD-rate on every stereo ladder when the reservoir's backoff was kept
     * -- and with bitRate == 0 the rate controller below never runs to claw
     * the quality back. */
    hEncoder->aacquantCfg.quality = baseQuality;

#ifdef FAAC_STATS
    if (attempt > 0)
    {
        g_faacStats.peakRetryFrames++;
        if (resBound) g_faacStats.resBoundRetryFrames++;
    }
    g_faacStats.totalQuality += hEncoder->aacquantCfg.quality;
#endif

    /* Adjust quality to get correct average bitrate */
    if (hEncoder->config.bitRate)
    {
        int desbits = numChannels * (hEncoder->config.bitRate * FRAME_LEN)
            / hEncoder->sampleRate;
        /* The reservoir answers for every bit that went out; the controller
           answers only for the ones the coder chose to spend, or a stuffed
           frame reads as on budget and quality never rises to replace the
           stuffing with signal. */
        int sentBits = payloadBits;
        int totalBits = payloadBits - hEncoder->stuffedBits;
        int reservoir = (hEncoder->resMean > 0);
        int sbrBits = 0;
        int sbrCharge;
        float fix;

        /* Settle the reservoir first; the header already declared this value
           through the same function. */
        if (reservoir)
        {
#ifdef FAAC_STATS
        {
            int raw = hEncoder->resFill + hEncoder->resMean - sentBits;
            if (raw < 0) g_faacStats.resUnderflowFrames++;
            g_faacStats.resFrames++;
            g_faacStats.resTotalBits += sentBits;
            if (hEncoder->resMinBits > 0 && hEncoder->stuffedBits > 0)
            {
                g_faacStats.resStuffFrames++;
                g_faacStats.resStuffBits += hEncoder->stuffedBits;
            }
            if (hEncoder->resCap > 0)
            {
                float pct = 100.0f * (float)hEncoder->resFill / (float)hEncoder->resCap;
                g_faacStats.totalResFill += pct;
                if (pct < g_faacStats.minResFill) g_faacStats.minResFill = pct;
                if (pct > g_faacStats.maxResFill) g_faacStats.maxResFill = pct;
            }
        }
#endif
        hEncoder->resFill = faacReservoirAfter(hEncoder, sentBits);
        }

        /* Exclude SBR's fixed overhead from the core budget so the rate
         * controller doesn't starve the core to pay for SBR. */
        sbrBits = SbrContextGetBits(hEncoder->sbrContext, NULL, (int)numChannels, (int)hEncoder->config.aacObjectType, 0);

        /* The payload is not the fixed overhead that comment assumes: measured
           at 48 kHz it runs 135-255 bits against a 2048-bit budget, swinging
           ~+/-3% of the frame. Subtracting the frame's own `sbrBits` makes the
           controller answer for a quantity it does not control -- a payload
           spike lands in the ledger as core debt the core never incurred, and
           `lend` then squeezes core quality to repay it. Holding the average
           instead lets a spike pass through to the output rate.

           Note the naive version of this cancels exactly and is not worth
           retrying: taking `sbrBits` off both sides of `diff` leaves
           `target - totalBits` unchanged. What breaks the cancellation is that
           the two subtracted quantities differ -- the ledger and `fix` see the
           average, while the frame really spent `sbrBits`.

           `sbrCharge` is the single substitution that switches between the two:
           the instantaneous payload (control the total: output tracks target,
           core quality chases SBR noise) or its average (control the core:
           steady core, SBR variance reaches the output rate). Both are
           defensible, so both are measured. On LC `sbrBits` is 0 every frame
           and the average is 0 with it, so this arm cannot move LC output. */
        {
            int prev = hEncoder->sbrBitsAcc;
            hEncoder->sbrBitsAcc = (prev < 0)
                ? (sbrBits << RC_SBR_EWMA_SHIFT)
                : (prev + sbrBits - (prev >> RC_SBR_EWMA_SHIFT));
        }
        sbrCharge = RC_SBR_USE_MEAN
            ? (hEncoder->sbrBitsAcc >> RC_SBR_EWMA_SHIFT)
            : sbrBits;

        /* Settle the frame against the account. The balance is signed: positive
           means the stream has banked bits it may still spend, negative means it
           has overspent and owes them back. Both directions are kept -- the
           previous code hid a draw from `fix` (the bits reached the stream while
           the loop was told the frame hit target), discarded deposits once the
           account was full, and zeroed any debt it could not cover. Credit was
           forgiven and debt was not, so the bias tracked how much each clip drew.

           One setpoint serves the account and `fix` alike; they had drifted
           apart, the account aiming under budget while `fix` aimed at it. It is
           a CORE setpoint: what is left of the frame's budget once SBR has been
           charged for, matched against what the core actually spent. */
        int coreTarget = (int)(desbits * RC_BALANCE_AIM) - sbrCharge;
        int coreBits = totalBits - sbrBits;
        int lend;
        int bound = RC_BALANCE_FRAMES * desbits;
        int diff = coreTarget - coreBits;
        hEncoder->bitBalance += diff;
        if (hEncoder->bitBalance > bound)
            hEncoder->bitBalance = bound;
        else if (hEncoder->bitBalance < -bound)
            hEncoder->bitBalance = -bound;

        /* What this frame may lean on, signed and amortized: a credit lets a
           complex frame overspend, a debt holds the next frames under budget
           until it is repaid. This is the smoothing the draw path was meant to
           provide, with the repayment it was missing. Amortizing is what keeps a
           deep balance from being worked off in one lurch. */
        lend = hEncoder->bitBalance / RC_BALANCE_AMORT;

        int floored = 0;
        int capped = 0;
        {
            int aim = coreTarget + lend;
#if RC_RESERVOIR_AIM
            if (reservoir)
            {
                /* resFill is already the fill after this frame, so these are
                   the next frame's floor and cap, less what SBR will take. */
                int coreFloor = hEncoder->resMean + hEncoder->resFill - hEncoder->resCap - sbrCharge;
                int coreCap = hEncoder->resMean + hEncoder->resFill - sbrCharge;
                if (RC_RESERVOIR_STUFF && aim < coreFloor)
                {
                    aim = coreFloor;
                    floored = 1;
                }
                if (aim > coreCap)
                    aim = coreCap;
                /* This frame took more than half the reservoir's slack, so a
                   frame its size will not fit the next cap. Correct down whole,
                   now, rather than let the next frame bust and be cut in a
                   retry: on speech that pre-emption is the difference between
                   1.5% and 28% of frames retrying, at the same accuracy. */
                capped = (coreBits > coreCap);
            }
#endif
            if (coreBits > 0)
                fix = (float)aim / (float)coreBits;
            else
                fix = 1.0f;
        }

        /* One damping. The stiffer damping this used to switch to when the
           account was far off centre, and the 0.5% deadband below, were both
           measured over the full corpus with the reservoir in place: neither
           moved BD-rate, accuracy or instruction count outside noise, so
           neither is here. */
        float damping = RC_DAMPING_FACTOR;
#ifdef FAAC_STATS
        {
            float fillPct = 50.0f + 50.0f * (float)hEncoder->bitBalance / (float)bound;
            g_faacStats.totalBalanceRatio += fillPct;
            if (fillPct < g_faacStats.minBalanceRatio) g_faacStats.minBalanceRatio = fillPct;
            if (fillPct > g_faacStats.maxBalanceRatio) g_faacStats.maxBalanceRatio = fillPct;
            g_faacStats.balanceFrames++;
        }
#endif

        /* Apply damping to the quality adjustment. A frame that will be
           stuffed up to the floor anyway spends those bits whether or not
           they carry signal, so the rise toward the floor is taken undamped
           and with a wider clamp: overshooting it costs nothing the frame
           would not have paid, and the cap catches a real overshoot. */
        if (floored)
        {
            if (fix > 1.5f) fix = 1.5f;
        }
        else if (capped)
        {
            if (fix < 0.5f) fix = 0.5f;
        }
        else
            fix = (fix - 1.0f) * damping + 1.0f;

#if RC_STUFF_HOLD
        if (fix > 1.0f && hEncoder->stuffedBits > 0 && hEncoder->rcPrevWant > 0
            && coreBits <= hEncoder->rcPrevWant + hEncoder->rcPrevWant / 50)
            fix = 1.0f;
#endif
        hEncoder->rcPrevWant = (hEncoder->stuffedBits > 0) ? coreBits : 0;

        if (!floored && !capped)
            fix = (fix < 0.80f) ? 0.80f : ((fix > 1.20f) ? 1.20f : fix);
        hEncoder->aacquantCfg.quality *= fix;

        if (hEncoder->aacquantCfg.quality > maxqual)
            hEncoder->aacquantCfg.quality = maxqual;
        if (hEncoder->aacquantCfg.quality < MINQUAL)
            hEncoder->aacquantCfg.quality = MINQUAL;

#ifdef FAAC_STATS
        {
            int overshot = (totalBits > desbits);
            g_faacStats.sumDesBits += desbits;
            if (overshot) g_faacStats.sumOverBits += (totalBits - desbits);
            else { g_faacStats.underFrames++; g_faacStats.sumUnderBits += (desbits - totalBits); }
            int floored = (hEncoder->aacquantCfg.quality <= MINQUAL);
            if (floored) g_faacStats.minqualFrames++;
            if (hEncoder->aacquantCfg.quality >= maxqual) g_faacStats.maxqualFrames++;
            if (overshot) g_faacStats.overshootFrames++;
            if (overshot && floored) g_faacStats.minqualOvershootFrames++;
        }
#endif
    }

    return frameBytes;
}


/* Scalefactorband data table for 1024 transform length */
SR_INFO srInfo[12+1] =
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
{ -1, 0, 0, {0}, {0} }
};
