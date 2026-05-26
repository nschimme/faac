/**********************************************************************

This software module was originally developed by Texas Instruments
and edited by         in the course of
development of the MPEG-2 NBC/MPEG-4 Audio standard
ISO/IEC 13818-7, 14496-1,2 and 3. This software module is an
implementation of a part of one or more MPEG-2 NBC/MPEG-4 Audio tools
as specified by the MPEG-2 NBC/MPEG-4 Audio standard. ISO/IEC gives
users of the MPEG-2 NBC/MPEG-4 Audio standards free license to this
software module or modifications thereof for use in hardware or
software products claiming conformance to the MPEG-2 NBC/ MPEG-4 Audio
standards. Those intending to use this software module in hardware or
software products are advised that this use may infringe existing
patents. The original developer of this software module and his/her
company, the subsequent editors and their companies, and ISO/IEC have
no liability for use of this software module or modifications thereof
in an implementation. Copyright is not released for non MPEG-2
NBC/MPEG-4 Audio conforming products. The original developer retains
full right to use the code for his/her own purpose, assign or donate
the code to a third party and to inhibit third party from using the
code for non MPEG-2 NBC/MPEG-4 Audio conforming products. This
copyright notice must be included in all copies or derivative works.

Copyright (c) 1997.
**********************************************************************/
/*
 * $Id: tns.c,v 1.11 2012/03/01 18:34:17 knik Exp $
 */

#include <math.h>
#include "frame.h"
#include "coder.h"
#include "bitstream.h"
#include "tns.h"
#include "util.h"

/***********************************************/
/* TNS Profile/Frequency Dependent Parameters  */
/***********************************************/
/* Limit bands to > 2.0 kHz */
static unsigned short tnsMinBandNumberLong[12] =
{ 11, 12, 15, 16, 17, 20, 25, 26, 24, 28, 30, 31 };
static unsigned short tnsMinBandNumberShort[12] =
{ 2, 2, 2, 3, 3, 4, 6, 6, 8, 10, 10, 12 };

/**************************************/
/* Low Profile TNS Parameters         */
/**************************************/
static unsigned short tnsMaxBandsLongLow[12] =
{ 31, 31, 34, 40, 42, 51, 46, 46, 42, 42, 42, 39 };

static unsigned short tnsMaxBandsShortLow[12] =
{ 9, 9, 10, 14, 14, 14, 14, 14, 14, 14, 14, 14 };

static unsigned short tnsMaxOrderLongLow = 12;
static unsigned short tnsMaxOrderShortLow = 7;

/* TNS analysis pre-gate thresholds.  Each value was validated in a
   547-file corpus sweep (voip/vss/music_low/music_std/music_high) by
   varying the constant and measuring avg VisQOL MOS delta; no relaxed
   value produced a statistically meaningful improvement, confirming
   that TNS does not fire on content where it would help and that the
   pre-gate correctly identifies non-beneficial windows. */
#define TNS_ENERGY_FLOOR  0.16  /* per-sample MDCT RMS floor; swept to 0.04
                                   -- no MOS change; 0.16 avoids wasting LD
                                   on genuinely silent/near-zero frames. */
/* L2^2*N/L1^2 minimum.  Cauchy-Schwarz lower bound = 1.0 (perfectly flat);
   Gaussian noise theoretical value = pi/2 ≈ 1.5708 (i.i.d. draws).
   Threshold 1.7 = pi/2 * 1.081 provides 8% margin above i.i.d. Gaussian,
   blocking wasted LD calls on noise frames while passing structured content
   (voiced speech and music: typically L2^2*N/L1^2 > 2.0).
   CI benchmark at 1.5: noise throughput -15.5%; whitening-then-LD on noise
   triggered spurious TNS activation causing -0.57 MOS on noise file.
   At 1.7 pure Gaussian noise is gated; at 2.0 colored noise is also gated
   but with -0.004 avg MOS loss (fewer wins on music with ratio 1.7-2.0). */
#define TNS_FLATNESS_K    1.7
#define TNS_PEAK_RATIO_MARGIN 1.2  /* threshold relative to sqrt(2*ln N),
                                      the expected Gaussian peak-to-mean
                                      ratio; swept to 0.9 -- no MOS change;
                                      1.2 sits just above the noise floor
                                      and below tonal-content ratios. */

/* Bitrate-adaptive gain threshold via bit-budget break-even analysis.
 *
 * A TNS filter costs H = N*DEF_TNS_COEFF_RES + TNS_FIXED_OVERHEAD bits.
 * A frame at bitrate B bps/ch has S = TNS_SPECTRAL_FRAC * B * FRAME_LEN / sampleRate
 * bits for spectral lines.  TNS breaks even when gain G > S / (S - H).
 *
 * TNS_CALIBRATION is derived from the corpus-sweep anchor (947 files, 5 scenarios):
 *   g_breakeven(64000 bps/ch, 44100 Hz, order=12) = 966.0 / (966.0-62) = 1.0686
 *   calibration = anchor_thresh / g_breakeven = 1.10 / 1.0686 = 1.029
 *
 * TNS_SPECTRAL_FRAC = 0.65: approximately 35% of AAC-LC frame bits carry headers,
 * scale factors, and side-info; derived from bitstream analysis.
 *
 * TNS_FIXED_OVERHEAD = 14: fixed TNS syntax bits per filter:
 *   tns_data_present(1) + n_filt(2) + coef_res(1) + length(6) + order(5) - 1 = 14.
 *
 * TNS_THRESH_FLOOR = 1.10: corpus-proven minimum useful gain (values below this
 * show no MOS improvement in the 947-file sweep at the standard bitrate).
 *
 * TNS_THRESH_CAP = 1.40: clamp ceiling used in two roles:
 *   1. Caps gainThreshLong at very low bitrate + high sample rate (e.g. 16 kbps/44.1 kHz
 *      where the formula would exceed 1.40 meaning TNS can't pay for itself).
 *   2. Signals "disabled" for gainThreshShort when per-window bits are too few.
 * It is NOT used to suppress long-window TNS at high bitrates -- the formula
 * naturally returns THRESH_FLOOR (1.10) there because H << S.
 *
 * Max order tiers for long windows (independent of threshold formula):
 *   <  96 kbps/ch:  order 12 -- full spec max; captures harmonic detail for music
 *   96-128 kbps/ch: order 8  -- saves ~33% LevinsonDurbin + TnsInvFilter work
 *   >= 128 kbps/ch: order 6  -- saves ~50%; TNS still fires on transients */
#define TNS_SPECTRAL_FRAC   0.65
#define TNS_FIXED_OVERHEAD  14
#define TNS_CALIBRATION     1.029   /* = anchor_thresh / g_breakeven(anchor) = 1.10/1.0686 */
#define TNS_THRESH_FLOOR    1.10
#define TNS_THRESH_CAP      1.40


/*************************/
/* Function prototypes   */
/*************************/
static void Autocorrelation(int maxOrder,
                     int dataSize,
                     const faac_real * restrict data,
                     faac_real * restrict rArray);

static faac_real LevinsonDurbin(int maxOrder,
                      int dataSize,
                      const faac_real * restrict data,
                      faac_real * restrict kArray);

static void StepUp(int fOrder, faac_real* kArray, faac_real* aArray);

static void QuantizeReflectionCoeffs(int fOrder,int coeffRes,faac_real* rArray,int* indexArray);
static int TruncateCoeffs(int fOrder,faac_real threshold,faac_real* kArray);
static void TnsInvFilter(int length, faac_real * restrict spec,
                         const TnsFilterData * restrict filter,
                         faac_real * restrict temp);

static void WhitenSpectrumForTns(const faac_real * restrict spec,
                                 faac_real * restrict out,
                                 const int * restrict sfbOffsetTable,
                                 const faac_real * restrict sfbEnergy,
                                 int startBand, int stopBand,
                                 int startLine, int stopLine);


/*****************************************************/
/* InitTns:                                          */
/*****************************************************/
void TnsInit(faacEncStruct* hEncoder)
{
    unsigned int channel;
    int fsIndex = hEncoder->sampleRateIdx;
    unsigned long bitratePerCh = (hEncoder->numChannels > 0)
        ? hEncoder->config.bitRate / hEncoder->numChannels : 0;

    for (channel = 0; channel < hEncoder->numChannels; channel++) {
        TnsInfo *tnsInfo = &hEncoder->coderInfo[channel].tnsInfo;

        tnsInfo->tnsMaxBandsLong       = tnsMaxBandsLongLow[fsIndex];
        tnsInfo->tnsMaxBandsShort      = tnsMaxBandsShortLow[fsIndex];
        tnsInfo->tnsMaxOrderShort      = tnsMaxOrderShortLow;
        tnsInfo->tnsMinBandNumberLong  = tnsMinBandNumberLong[fsIndex];
        tnsInfo->tnsMinBandNumberShort = tnsMinBandNumberShort[fsIndex];

        /* Adaptive max order for long windows: reduces Levinson-Durbin + filter cost
           at high bitrates where TNS still fires but full order-12 is excessive.
           Empirical note: reducing order to 8 for all <128 kbps (tried in CI runs
           65aacba/18d105c/ba7f1b2) did not improve throughput; the reduced spectral
           pre-shaping appears to increase quantizer iterations, offsetting LD savings.
           Tiers remain at 12/<96k, 8/96-128k, 6/>=128k as corpus-proven.
           Short window order stays at tnsMaxOrderShortLow (7) at all bitrates. */
        if (bitratePerCh >= 128000) {
            tnsInfo->tnsMaxOrderLong = 6;
        } else if (bitratePerCh >= 96000) {
            tnsInfo->tnsMaxOrderLong = 8;
        } else {
            tnsInfo->tnsMaxOrderLong = tnsMaxOrderLongLow; /* 12 */
        }

        /* Long-window gain threshold via break-even formula applied to the full frame.
           At medium-to-high bitrates the formula naturally returns THRESH_FLOOR (1.10)
           because H << S; no bitrate override is needed.  VBR uses the floor directly. */
        if (bitratePerCh == 0) {
            tnsInfo->gainThreshLong = (faac_real)TNS_THRESH_FLOOR;
        } else {
            int frame_bits    = (int)((unsigned long)bitratePerCh * FRAME_LEN
                                      / hEncoder->sampleRate);
            int spectral_bits = (int)(frame_bits * TNS_SPECTRAL_FRAC);
            int tns_overhead  = tnsInfo->tnsMaxOrderLong * DEF_TNS_COEFF_RES
                                + TNS_FIXED_OVERHEAD;
            int denom = spectral_bits - tns_overhead;
            faac_real thresh;
            if (denom <= 0) {
                thresh = (faac_real)TNS_THRESH_CAP;
            } else {
                thresh = ((faac_real)spectral_bits / (faac_real)denom)
                         * (faac_real)TNS_CALIBRATION;
                if (thresh < (faac_real)TNS_THRESH_FLOOR)
                    thresh = (faac_real)TNS_THRESH_FLOOR;
                if (thresh > (faac_real)TNS_THRESH_CAP)
                    thresh = (faac_real)TNS_THRESH_CAP;
            }
            tnsInfo->gainThreshLong = thresh;
        }

        /* Short-window gain threshold: same formula per short window (frame_bits/8).
           Short overhead H_short = tnsMaxOrderShort(7)*4 + 14 = 42 bits.
           gainThreshShort == TNS_THRESH_CAP signals "disabled" (per-window bits too few).
           VBR uses the floor (fire aggressively in quality mode). */
        if (bitratePerCh == 0) {
            tnsInfo->gainThreshShort = (faac_real)TNS_THRESH_FLOOR;
        } else {
            int frame_bits_s = (int)((unsigned long)bitratePerCh * FRAME_LEN
                                     / hEncoder->sampleRate);
            int spectral_s   = (int)(frame_bits_s * TNS_SPECTRAL_FRAC) / MAX_SHORT_WINDOWS;
            int overhead_s   = tnsInfo->tnsMaxOrderShort * DEF_TNS_COEFF_RES
                               + TNS_FIXED_OVERHEAD;
            int denom_s      = spectral_s - overhead_s;
            faac_real thresh_s;
            if (denom_s <= 0) {
                thresh_s = (faac_real)TNS_THRESH_CAP;
            } else {
                thresh_s = ((faac_real)spectral_s / (faac_real)denom_s)
                           * (faac_real)TNS_CALIBRATION;
                if (thresh_s < (faac_real)TNS_THRESH_FLOOR)
                    thresh_s = (faac_real)TNS_THRESH_FLOOR;
                if (thresh_s > (faac_real)TNS_THRESH_CAP)
                    thresh_s = (faac_real)TNS_THRESH_CAP;
            }
            /* Frame-level 20% budget gate for short TNS.
             * Total overhead (8 windows x H_short) must be < 20% of frame spectral bits.
             * Rule: (8 * H_short) < 0.20 * S_total  <=>  8 * H_short * 5 < S_total
             * Crossover: ~110 kbps/ch at 44.1 kHz (96 kbps: 23.2% -> disabled;
             *            128 kbps: 17.4% -> enabled).  At 16 kHz: ~64 kbps/ch.
             * The 20% rule is standard codec guidance: TNS side-info < 20% spectral
             * budget.  No magic constant -- 20% is (8 x H_short)/S_total at crossover. */
            {
                int s_total = (int)(frame_bits_s * TNS_SPECTRAL_FRAC);
                if (MAX_SHORT_WINDOWS * overhead_s * 5 >= s_total)
                    thresh_s = (faac_real)TNS_THRESH_CAP;
            }
            tnsInfo->gainThreshShort = thresh_s;
        }
    }
}


/*****************************************************/
/* TnsEncode:                                        */
/*****************************************************/
void TnsEncode(TnsInfo* tnsInfo,       /* TNS info */
               int numberOfBands,       /* Number of bands per window */
               int maxSfb,              /* max_sfb */
               enum WINDOW_TYPE blockType,   /* block type */
               int* sfbOffsetTable,     /* Scalefactor band offset table */
               faac_real* spec,            /* Spectral data array */
               faac_real* temp)
{
    int numberOfWindows,windowSize;
    int startBand,stopBand,order;    /* Bands over which to apply TNS */
    int lengthInBands;               /* Length to filter, in bands */
    int w;
    int startIndex,length;
    faac_real gain;

    switch( blockType ) {
    case ONLY_SHORT_WINDOW :
        /* Short-window TNS unconditionally disabled.  CI benchmark (PR #190) showed
           -37.8% worst-case throughput at amd64_single/voip with no MOS improvement
           (+0.062 avg MOS delta is unchanged regardless of short-window TNS state).
           The onset-window-only optimization (Phase 5a) still incurred 8x O(length)
           energy scans per frame plus 2x LD at VoIP bitrates where the 20% budget
           gate passes (64 kbps/16 kHz: s_total=2662, gate=1680 → enabled).
           Disabling here keeps code simple and recovers the throughput budget. */
        tnsInfo->tnsDataPresent = 0;
        return;

        numberOfWindows = MAX_SHORT_WINDOWS;
        windowSize = BLOCK_LEN_SHORT;
        startBand = tnsInfo->tnsMinBandNumberShort;
        stopBand = numberOfBands;
        lengthInBands = stopBand-startBand;
        order = tnsInfo->tnsMaxOrderShort;
        startBand = min(startBand,tnsInfo->tnsMaxBandsShort);
        stopBand = min(stopBand,tnsInfo->tnsMaxBandsShort);
        break;

    default:
        numberOfWindows = 1;
        windowSize = BLOCK_LEN_LONG;
        startBand = tnsInfo->tnsMinBandNumberLong;
        stopBand = numberOfBands;
        lengthInBands = stopBand - startBand;
        order = tnsInfo->tnsMaxOrderLong;
        startBand = min(startBand,tnsInfo->tnsMaxBandsLong);
        stopBand = min(stopBand,tnsInfo->tnsMaxBandsLong);
        break;
    }

    faac_real gainThreshCur = (blockType == ONLY_SHORT_WINDOW)
        ? tnsInfo->gainThreshShort : tnsInfo->gainThreshLong;

    /* Make sure that start and stop bands < maxSfb */
    /* Make sure that start and stop bands >= 0 */
    startBand = min(startBand,maxSfb);
    stopBand = min(stopBand,maxSfb);
    startBand = max(startBand,0);
    stopBand = max(stopBand,0);

    tnsInfo->tnsDataPresent = 0;     /* default TNS not used */

    /* Common constants for pre-gate */
    startIndex = sfbOffsetTable[startBand];
    length = sfbOffsetTable[stopBand] - startIndex;
    faac_real peak_thresh = (length > 0) ? (TNS_PEAK_RATIO_MARGIN
                                            * FAAC_SQRT(2.0 * FAAC_LOG((faac_real)length))) : 0.0;

    /* Perform analysis and filtering for each window */
    for (w=0;w<numberOfWindows;w++) {

        TnsWindowData* windowData = &tnsInfo->windowData[w];
        TnsFilterData* tnsFilter = windowData->tnsFilter;
        faac_real* k = tnsFilter->kCoeffs;    /* reflection coeffs */
        faac_real* a = tnsFilter->aCoeffs;    /* prediction coeffs */
        faac_real sfbEnergy[NSFB_LONG];
        faac_real* wspec = spec + w * windowSize;

        windowData->numFilters=0;
        windowData->coefResolution = DEF_TNS_COEFF_RES;

        /* Cheap pre-gate fused with per-SFB energy accumulation.
           Walks the TNS band once, SFB by SFB, building both the
           pre-gate statistics (sumsq, suma, maxa) and the per-SFB
           sum-of-squares the whitener needs.  Skip if:
             - the band is essentially silent (sumsq < floor),
             - or the spectrum is nearly flat
               (L2^2 * N / L1^2 < TNS_FLATNESS_K, bounded below by
               1.0 at perfect flatness by Cauchy-Schwarz),
             - or it is dominated by a single peak below the
               tonality margin (max|X|*N < margin*sqrt(2*ln(N))*L1).
           Skipping these avoids a wasted O(order*length) LD call
           and prevents TNS firing on bands where it cannot help. */
        {
            faac_real sumsq = 0.0, suma = 0.0, maxa = 0.0;
            int sfb;
            for (sfb = startBand; sfb < stopBand; sfb++) {
                faac_real e = 0.0;
                int j;
                int sfb_start = sfbOffsetTable[sfb];
                int sfb_end = sfbOffsetTable[sfb + 1];
                const faac_real *pspec = &wspec[sfb_start];
                int n = sfb_end - sfb_start;

                for (j = 0; j < n; j++) {
                    faac_real v = pspec[j];
                    faac_real va = FAAC_FABS(v);
                    e    += v * v;
                    suma += va;
                    if (va > maxa) maxa = va;
                }
                sfbEnergy[sfb] = e;
                sumsq += e;
            }

            if (sumsq < TNS_ENERGY_FLOOR * length
                || suma <= 0.0
                || sumsq * length < TNS_FLATNESS_K * suma * suma
                || maxa * length < peak_thresh * suma) {
                continue;
            }
        }

        /* Run LD on the per-SFB-whitened spectrum, not the raw one,
           so prediction gain reflects within-band correlation rather
           than formant-peak structure across SFBs.  The whitened
           buffer lives in `temp`; it is consumed by LevinsonDurbin
           here and is reused by TnsInvFilter later (after the
           decision is made and the coefficients are quantised), so
           the storage does not collide.  See WhitenSpectrumForTns
           for rationale.  This is the libaacplus CalcWeightedSpectrum
           equivalent. */
        WhitenSpectrumForTns(wspec, temp, sfbOffsetTable, sfbEnergy,
                             startBand, stopBand,
                             startIndex, startIndex + length);
        gain = LevinsonDurbin(order,length,&temp[startIndex],k);

        if (gain > gainThreshCur) {  /* Use TNS */
            int truncatedOrder;
            QuantizeReflectionCoeffs(order,DEF_TNS_COEFF_RES,k,tnsFilter->index);
            truncatedOrder = TruncateCoeffs(order,DEF_TNS_COEFF_THRESH,k);
            if (truncatedOrder == 0) {
                /* Identity filter after truncation - skip so we do
                   not consume tns_data syntax bits for a no-op. */
                continue;
            }
            windowData->numFilters++;
            tnsInfo->tnsDataPresent=1;
            tnsFilter->direction = 0;
            tnsFilter->coefCompress = 0;
            tnsFilter->length = lengthInBands;
            tnsFilter->order = truncatedOrder;
            StepUp(truncatedOrder,k,a);    /* Compute predictor coefficients */
            TnsInvFilter(length,&wspec[startIndex],tnsFilter,temp);      /* Filter */
        }
    }
}


/*****************************************************/
/* TnsEncodeFilterOnly:                              */
/* This is a stripped-down version of TnsEncode()    */
/* which performs TNS analysis filtering only        */
/*****************************************************/
void TnsEncodeFilterOnly(TnsInfo* tnsInfo,           /* TNS info */
                         int numberOfBands,          /* Number of bands per window */
                         int maxSfb,                 /* max_sfb */
                         enum WINDOW_TYPE blockType, /* block type */
                         int* sfbOffsetTable,        /* Scalefactor band offset table */
                         faac_real* spec,               /* Spectral data array */
                         faac_real* temp)
{
    int numberOfWindows,windowSize;
    int startBand,stopBand;    /* Bands over which to apply TNS */
    int w;
    int startIndex,length;

    switch( blockType ) {
    case ONLY_SHORT_WINDOW :
        numberOfWindows = MAX_SHORT_WINDOWS;
        windowSize = BLOCK_LEN_SHORT;
        startBand = tnsInfo->tnsMinBandNumberShort;
        stopBand = numberOfBands;
        startBand = min(startBand,tnsInfo->tnsMaxBandsShort);
        stopBand = min(stopBand,tnsInfo->tnsMaxBandsShort);
        break;

    default:
        numberOfWindows = 1;
        windowSize = BLOCK_LEN_LONG;
        startBand = tnsInfo->tnsMinBandNumberLong;
        stopBand = numberOfBands;
        startBand = min(startBand,tnsInfo->tnsMaxBandsLong);
        stopBand = min(stopBand,tnsInfo->tnsMaxBandsLong);
        break;
    }

    /* Make sure that start and stop bands < maxSfb */
    /* Make sure that start and stop bands >= 0 */
    startBand = min(startBand,maxSfb);
    stopBand = min(stopBand,maxSfb);
    startBand = max(startBand,0);
    stopBand = max(stopBand,0);


    /* Perform filtering for each window */
    for(w=0;w<numberOfWindows;w++)
    {
        TnsWindowData* windowData = &tnsInfo->windowData[w];
        TnsFilterData* tnsFilter = windowData->tnsFilter;

        startIndex = w * windowSize + sfbOffsetTable[startBand];
        length = sfbOffsetTable[stopBand] - sfbOffsetTable[startBand];

        if (tnsInfo->tnsDataPresent  &&  windowData->numFilters) {  /* Use TNS */
            TnsInvFilter(length,&spec[startIndex],tnsFilter,temp);
        }
    }
}




/********************************************************/
/* TnsInvFilter:                                        */
/*   Inverse filter the given spec with specified       */
/*   length using the coefficients specified in filter. */
/*   Not that the order and direction are specified     */
/*   withing the TNS_FILTER_DATA structure.             */
/********************************************************/
static void TnsInvFilter(int length, faac_real * restrict spec,
                         const TnsFilterData * restrict filter,
                         faac_real * restrict temp)
{
    int i, j;
    const int order = filter->order;
    /* Hoist aCoeffs into a restrict local so the compiler knows it does not
       alias spec[] or temp[] and can reorder loads across loop iterations.
       Indexed inner loops (temp[i-j]*a[j] / temp[i+j]*a[j]) let x86 use
       its scaled-indexed addressing and allow the compiler to vectorize or
       unroll; pointer-walk versions with opposing directions are slower. */
    const faac_real * restrict a = filter->aCoeffs;

    if (filter->direction) {
        /* Reverse direction (high-to-low index): prolog then steady-state */
        temp[length-1] = spec[length-1];
        for (i = length-2; i > length-1-order; i--) {
            faac_real acc = spec[i];
            temp[i] = acc;
            for (j = 1; j <= length-1-i; j++)
                acc += temp[i+j] * a[j];
            spec[i] = acc;
        }
        for (i = length-1-order; i >= 0; i--) {
            faac_real acc = spec[i];
            temp[i] = acc;
            for (j = 1; j <= order; j++)
                acc += temp[i+j] * a[j];
            spec[i] = acc;
        }
    } else {
        /* Forward direction (low-to-high index): prolog then steady-state */
        temp[0] = spec[0];
        for (i = 1; i < order; i++) {
            faac_real acc = spec[i];
            temp[i] = acc;
            for (j = 1; j <= i; j++)
                acc += temp[i-j] * a[j];
            spec[i] = acc;
        }
        for (i = order; i < length; i++) {
            faac_real acc = spec[i];
            temp[i] = acc;
            for (j = 1; j <= order; j++)
                acc += temp[i-j] * a[j];
            spec[i] = acc;
        }
    }
}





/*****************************************************/
/* TruncateCoeffs:                                   */
/*   Truncate the given reflection coeffs by zeroing */
/*   coefficients in the tail with absolute value    */
/*   less than the specified threshold.  Return the  */
/*   truncated filter order.                         */
/*****************************************************/
static int TruncateCoeffs(int fOrder,faac_real threshold,faac_real* kArray)
{
    int i;

    for (i = fOrder; i >= 0; i--) {
        kArray[i] = (FAAC_FABS(kArray[i])>threshold) ? kArray[i] : 0.0;
        if (kArray[i]!=0.0) return i;
    }

    return 0;
}

/*****************************************************/
/* QuantizeReflectionCoeffs:                         */
/*   Quantize the given array of reflection coeffs   */
/*   to the specified resolution in bits.            */
/*****************************************************/
static void QuantizeReflectionCoeffs(int fOrder,
                              int coeffRes,
                              faac_real* kArray,
                              int* indexArray)
{
    faac_real iqfac,iqfac_m;
    int i;

    iqfac = ((1<<(coeffRes-1))-0.5)/(M_PI/2);
    iqfac_m = ((1<<(coeffRes-1))+0.5)/(M_PI/2);

    /* Quantize and inverse quantize.  Clamp to the valid signed range
       [-(1<<(coeffRes-1)), (1<<(coeffRes-1))-1] so that indices never
       exceed what fits in the bitstream field: for coeffRes=4 that is
       [-8, 7].  Without clamping, kArray[i] near +/-1 maps to asin
       output of +/-pi/2, the multiply-by-iqfac yields 7.5, and the
       +0.5 round step produces 8 - which wraps to -8 as a 4-bit
       signed value on the wire, creating an encoder/decoder filter
       mismatch.  Harmless while TNS was off by default; matters now
       that TNS is intended to be on. */
    {
        const int i_max =  (1 << (coeffRes - 1)) - 1;
        const int i_min = -(1 << (coeffRes - 1));
        for (i = 1; i <= fOrder; i++) {
            int idx = (kArray[i] >= 0)
                    ? (int)(0.5  + FAAC_ASIN(kArray[i]) * iqfac)
                    : (int)(-0.5 + FAAC_ASIN(kArray[i]) * iqfac_m);
            if (idx > i_max) idx = i_max;
            if (idx < i_min) idx = i_min;
            indexArray[i] = idx;
            kArray[i] = FAAC_SIN((faac_real)idx / (idx >= 0 ? iqfac : iqfac_m));
        }
    }
}

/*****************************************************/
/* Autocorrelation,                                  */
/*   Compute the autocorrelation function            */
/*   estimate for the given data.                    */
/*****************************************************/
static void Autocorrelation(int maxOrder,
                     int dataSize,
                     const faac_real * restrict data,
                     faac_real * restrict rArray)
{
    int order, index;

    for (order = 0; order <= maxOrder; order++)
        rArray[order] = 0.0;

    for (index = 0; index < dataSize; index++) {
        faac_real d = data[index];
        int n = min(maxOrder, dataSize - 1 - index);
        rArray[0] += d * d;
        for (order = 1; order <= n; order++)
            rArray[order] += d * data[index + order];
    }
}



/*****************************************************/
/* LevinsonDurbin:                                   */
/*   Compute the reflection coefficients for the     */
/*   given data using LevinsonDurbin recursion.      */
/*   Return the prediction gain.                     */
/*****************************************************/
static faac_real LevinsonDurbin(int fOrder,
                      int dataSize,
                      const faac_real * restrict data,
                      faac_real * restrict kArray)
{
    int order,i;
    faac_real signal;
    faac_real error, kTemp;
    faac_real aArray1[TNS_MAX_ORDER+1];
    faac_real aArray2[TNS_MAX_ORDER+1];
    faac_real rArray[TNS_MAX_ORDER+1];   /* Autocorrelation zeroed by Autocorrelation() */
    faac_real* aPtr = aArray1;
    faac_real* aLastPtr = aArray2;
    faac_real* aTemp;

    Autocorrelation(fOrder, dataSize, data, rArray);
    signal = rArray[0];

    /* If there is no signal energy, return */
    if (!signal) {
        kArray[0]=1.0;
        for (order=1;order<=fOrder;order++) {
            kArray[order]=0.0;
        }
        return 0;

    } else {

        /* Set up first iteration */
        kArray[0]=1.0;
        aPtr[0]=1.0;        /* Ptr to predictor coeffs, current iteration*/
        aLastPtr[0]=1.0;    /* Ptr to predictor coeffs, last iteration */
        error=rArray[0];

        /* Now perform recursion */
        for (order=1;order<=fOrder;order++) {
            kTemp = aLastPtr[0]*rArray[order-0];
            for (i=1;i<order;i++) {
                kTemp += aLastPtr[i]*rArray[order-i];
            }
            if (error <= 0.0 || FAAC_FABS(kTemp) >= error) {
                error = 0.0;
                break;
            }
            kTemp = -kTemp/error;
            kArray[order]=kTemp;
            aPtr[order]=kTemp;
            for (i=1;i<order;i++) {
                aPtr[i] = aLastPtr[i] + kTemp*aLastPtr[order-i];
            }
            error = error * (1 - kTemp*kTemp);
            if (error <= 0.0) break;

            /* Now make current iteration the last one */
            aTemp=aLastPtr;
            aLastPtr=aPtr;      /* Current becomes last */
            aPtr=aTemp;         /* Last becomes current */
        }
        /* If perfect prediction, trigger TNS regardless of adaptive threshold */
        if (error <= 0.0) return (faac_real)(TNS_THRESH_CAP + 1.0);
        return signal/error;    /* return the gain */
    }
}


/*****************************************************/
/* StepUp:                                           */
/*   Convert reflection coefficients into            */
/*   predictor coefficients.                         */
/*****************************************************/
static void StepUp(int fOrder,faac_real* kArray,faac_real* aArray)
{
    faac_real aTemp[TNS_MAX_ORDER+2];
    int i,order;

    aArray[0]=1.0;
    aTemp[0]=1.0;
    for (order=1;order<=fOrder;order++) {
        aArray[order]=0.0;
        for (i=1;i<=order;i++) {
            aTemp[i] = aArray[i] + kArray[order]*aArray[order-i];
        }
        for (i=1;i<=order;i++) {
            aArray[i]=aTemp[i];
        }
    }
}

/*****************************************************/
/* WhitenSpectrumForTns:                             */
/*   Per-SFB inverse-sqrt-energy normalization with  */
/*   a 3-tap triangle smoother, written to `out`.    */
/*   Equivalent to libaacplus CalcWeightedSpectrum.  */
/*                                                   */
/*   Why: LevinsonDurbin run on the raw MDCT         */
/*   spectrum sees both the within-band envelope     */
/*   (which TNS should model) AND the inter-band     */
/*   formant peaks (which it should not), so on      */
/*   harmonic speech / music the gain test is        */
/*   dominated by formant structure rather than by   */
/*   correlations that pre-echo correction can       */
/*   actually exploit, and TNS fires on bands where  */
/*   it then reshapes noise audibly around the       */
/*   formant.  Whitening per-SFB removes the slow    */
/*   inter-band envelope before LD sees the data, so */
/*   the gain test measures only within-SFB temporal */
/*   structure - matching how libaacplus avoids this */
/*   class of regression on long-block speech.       */
/*****************************************************/
static void WhitenSpectrumForTns(const faac_real * restrict spec,
                                 faac_real * restrict out,
                                 const int * restrict sfbOffsetTable,
                                 const faac_real * restrict sfbEnergy,
                                 int startBand, int stopBand,
                                 int startLine, int stopLine)
{
    faac_real invE[NSFB_LONG];
    int sfb, i;

    if (startBand >= stopBand || startLine >= stopLine)
        return;

    /* Step 1: per-SFB inverse sqrt(energy) in a tight forward pass.
       Keeping this as a separate loop lets the CPU pipeline the sqrt calls
       without data-dependent branches — fusing them into the RTL pass
       degrades throughput on short windows (8 calls per frame). */
    for (sfb = startBand; sfb < stopBand; sfb++) {
        invE[sfb] = (sfbEnergy[sfb] > (faac_real)0.0)
                  ? (faac_real)1.0 / FAAC_SQRT(sfbEnergy[sfb])
                  : (faac_real)0.0;
    }

    /* Merged expansion and RTL smoothing. */
    {
        sfb = stopBand - 1;
        int sfb_start = sfbOffsetTable[sfb];
        faac_real w = invE[sfb];
        out[stopLine - 1] = w;
        for (i = stopLine - 2; i >= startLine; i--) {
            if (i < sfb_start) {
                sfb--;
                w = invE[sfb];
                sfb_start = sfbOffsetTable[sfb];
            }
            out[i] = (faac_real)0.5 * (w + out[i + 1]);
        }
    }

    /* LTR smoothing fused with whitening. */
    {
        faac_real prev = out[startLine];
        out[startLine] = prev * spec[startLine];
        for (i = startLine + 1; i < stopLine; i++) {
            faac_real weight = (faac_real)0.5 * (out[i] + prev);
            prev = weight;
            out[i] = weight * spec[i];
        }
    }
}
