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
#ifdef FAAC_TNS_TUNING
#include <stdio.h>
#include <stdlib.h>
#endif
#include "frame.h"
#include "coder.h"
#include "bitstream.h"
#include "tns.h"
#include "util.h"

#ifdef FAAC_TNS_TUNING
static long tns_analyzed_count = 0;
static long tns_applied_count  = 0;

void TnsPrintStats(void)
{
    fprintf(stderr, "TNS_ACTIVATION applied=%ld analyzed=%ld rate=%.3f\n",
            tns_applied_count, tns_analyzed_count,
            tns_analyzed_count > 0
                ? (double)tns_applied_count / tns_analyzed_count
                : 0.0);
}
#endif

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


static unsigned short tnsMaxOrderLongLow  = 8;
static unsigned short tnsMaxOrderShortLow = 7;

/* TNS break-even gain analysis constants. */
#define TNS_SPECTRAL_FRAC   0.65    /* Estimated fraction of frame bits for spectral data */
#define TNS_FIXED_OVERHEAD  14      /* Fixed bitstream overhead per TNS filter */
#define TNS_CALIBRATION     1.029   /* Calibration factor against corpus anchor */
#define TNS_THRESH_FLOOR    1.10    /* Minimum gain threshold for TNS utility */
#define TNS_THRESH_CAP      1.80    /* Maximum adaptive threshold cap */

/*************************/
/* Function prototypes   */
/*************************/
static void Autocorrelation(int maxOrder,                    /* Maximum autocorr order */
                     int dataSize,                    /* Size of the data array */
                     const faac_real * restrict data, /* Data array */
                     faac_real * restrict rArray);    /* Autocorrelation array */

static faac_real LevinsonDurbin(int maxOrder,                    /* Maximum filter order */
                      int dataSize,                    /* Size of the data array */
                      const faac_real * restrict data, /* Data array */
                      faac_real * restrict kArray);    /* Reflection coeff array */

static void StepUp(int fOrder, faac_real* kArray, faac_real* aArray);

static void QuantizeReflectionCoeffs(int fOrder,int coeffRes,faac_real* rArray,int* indexArray);
static int TruncateCoeffs(int fOrder,faac_real threshold,faac_real* kArray);
static void TnsInvFilter(int length, faac_real * restrict spec,
                         const TnsFilterData * restrict filter,
                         faac_real * restrict temp);

/*****************************************************/
/* InitTns:                                          */
/*****************************************************/
void TnsInit(faacEncStruct* hEncoder)
{
    unsigned int channel;
    int fsIndex = hEncoder->sampleRateIdx;
    /* hEncoder->config.bitRate is already normalized to bps per channel
     * by the frontend and faacEncSetConfiguration. */
    unsigned long bitratePerCh = hEncoder->config.bitRate;
    unsigned long quality = hEncoder->config.quantqual;
    unsigned long effectiveBitratePerCh;

    if (bitratePerCh > 0) {
        effectiveBitratePerCh = bitratePerCh;
    } else {
        /* Estimate effective bitrate from quality for VBR gating and thresholds.
         * Project anchor: Quality 100 is approx 64kbps/ch for stereo 44.1kHz. */
        effectiveBitratePerCh = (quality * 1280) / hEncoder->numChannels;
    }

    /* Sweepable constants: defaults match the #defines / compile-time values.
     * Under FAAC_TNS_TUNING these are overridable via environment variables
     * so run_benchmark.py --sweep can drive them without recompiling. */
#ifdef FAAC_TNS_TUNING
    unsigned long t_gate_bps   = 64000UL;
    faac_real t_spectral_frac  = (faac_real)TNS_SPECTRAL_FRAC;
    faac_real t_calibration    = (faac_real)TNS_CALIBRATION;
    faac_real t_thresh_floor   = (faac_real)TNS_THRESH_FLOOR;
    faac_real t_thresh_cap     = (faac_real)TNS_THRESH_CAP;
    int       t_maxorder       = tnsMaxOrderLongLow;
    { const char *e;
      if ((e = getenv("FAAC_TNS_GATE_BPS")))         t_gate_bps      = (unsigned long)atof(e);
      if ((e = getenv("FAAC_TNS_SPECTRAL_FRAC")))    t_spectral_frac = (faac_real)atof(e);
      if ((e = getenv("FAAC_TNS_CALIBRATION")))      t_calibration   = (faac_real)atof(e);
      if ((e = getenv("FAAC_TNS_GAINTHRESH_FLOOR"))) t_thresh_floor  = (faac_real)atof(e);
      if ((e = getenv("FAAC_TNS_GAINTHRESH_CAP")))   t_thresh_cap    = (faac_real)atof(e);
      if ((e = getenv("FAAC_TNS_MAXORDER")))          t_maxorder      = (int)atof(e);
    }
#else
    unsigned long t_gate_bps   = 64000UL;
    faac_real t_spectral_frac  = (faac_real)TNS_SPECTRAL_FRAC;
    faac_real t_calibration    = (faac_real)TNS_CALIBRATION;
    faac_real t_thresh_floor   = (faac_real)TNS_THRESH_FLOOR;
    faac_real t_thresh_cap     = (faac_real)TNS_THRESH_CAP;
    int       t_maxorder       = tnsMaxOrderLongLow;
#endif

    /* TNS gate: active only when enabled and within bitrate limits. */
    int tnsGated = (effectiveBitratePerCh >= t_gate_bps);
    hEncoder->config.useTns = (hEncoder->config.useTns != 0) && !tnsGated;

    for (channel = 0; channel < hEncoder->numChannels; channel++) {
        TnsInfo *tnsInfo = &hEncoder->coderInfo[channel].tnsInfo;

        tnsInfo->tnsMaxBandsLong       = tnsMaxBandsLongLow[fsIndex];
        tnsInfo->tnsMaxBandsShort      = tnsMaxBandsShortLow[fsIndex];
        tnsInfo->tnsMaxOrderShort      = tnsMaxOrderShortLow;
        tnsInfo->tnsMinBandNumberLong  = tnsMinBandNumberLong[fsIndex];
        tnsInfo->tnsMinBandNumberShort = tnsMinBandNumberShort[fsIndex];

        /* Internal TNS state: disabled if gated or explicitly forced off. */
        tnsInfo->tnsDisabled = !hEncoder->config.useTns;

        if (tnsInfo->tnsDisabled) {
            continue;
        }

        tnsInfo->tnsMaxOrderLong = t_maxorder;

        /* Long-window gain threshold via break-even bit budget formula. */
        {
            int frame_bits    = (int)(effectiveBitratePerCh * FRAME_LEN
                                      / hEncoder->sampleRate);
            int spectral_bits = (int)(frame_bits * t_spectral_frac);
            int tns_overhead  = tnsInfo->tnsMaxOrderLong * DEF_TNS_COEFF_RES
                                + TNS_FIXED_OVERHEAD;
            int denom = spectral_bits - tns_overhead;
            faac_real thresh;
            if (denom <= 0) {
                thresh = t_thresh_cap;
            } else {
                thresh = ((faac_real)spectral_bits / (faac_real)denom)
                         * t_calibration;
                if (thresh < t_thresh_floor)
                    thresh = t_thresh_floor;
                if (thresh > t_thresh_cap)
                    thresh = t_thresh_cap;
            }
            tnsInfo->gainThreshLong = thresh;
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

    if (tnsInfo->tnsDisabled) {
        tnsInfo->tnsDataPresent = 0;
        return;
    }
    int startBand,stopBand,order;    /* Bands over which to apply TNS */
    int lengthInBands;               /* Length to filter, in bands */
    int w, i;
    int startIndex,length;
    faac_real gain;

    switch( blockType ) {
    case ONLY_SHORT_WINDOW :
        /* Analysis disabled for short windows to balance throughput. */
        tnsInfo->tnsDataPresent = 0;
        return;

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

    /* Make sure that start and stop bands < maxSfb */
    /* Make sure that start and stop bands >= 0 */
    startBand = min(startBand,maxSfb);
    stopBand = min(stopBand,maxSfb);
    startBand = max(startBand,0);
    stopBand = max(stopBand,0);

    tnsInfo->tnsDataPresent = 0;     /* default TNS not used */
#ifdef FAAC_TNS_TUNING
    tns_analyzed_count++;
#endif

    /* Perform analysis and filtering for each window */
    for (w=0;w<numberOfWindows;w++) {

        TnsWindowData* windowData = &tnsInfo->windowData[w];
        TnsFilterData* tnsFilter = windowData->tnsFilter;
        faac_real* k = tnsFilter->kCoeffs;    /* reflection coeffs */
        faac_real* a = tnsFilter->aCoeffs;    /* prediction coeffs */

        windowData->numFilters=0;
        windowData->coefResolution = DEF_TNS_COEFF_RES;
        startIndex = w * windowSize + sfbOffsetTable[startBand];
        length = sfbOffsetTable[stopBand] - sfbOffsetTable[startBand];
        gain = LevinsonDurbin(order,length,&spec[startIndex],k);

        if (gain > tnsInfo->gainThreshLong) {  /* Use TNS */
            int truncatedOrder;
            QuantizeReflectionCoeffs(order,DEF_TNS_COEFF_RES,tnsFilter->kCoeffs,tnsFilter->index);
            truncatedOrder = TruncateCoeffs(order,DEF_TNS_COEFF_THRESH,tnsFilter->kCoeffs);
            if (truncatedOrder == 0) continue;

            windowData->numFilters++;
            tnsInfo->tnsDataPresent=1;
            tnsFilter->direction = 0;

            /* Lossless bitstream compression:
             * Signal coefCompress=1 if all transmitted indices fit in 3 bits. */
            tnsFilter->coefCompress = 1;
            for (i = 1; i <= truncatedOrder; i++) {
                if (tnsFilter->index[i] < -4 || tnsFilter->index[i] > 3) {
                    tnsFilter->coefCompress = 0;
                    break;
                }
            }

            tnsFilter->length = lengthInBands;
            tnsFilter->order = truncatedOrder;
            StepUp(truncatedOrder,k,a);    /* Compute predictor coefficients */
            TnsInvFilter(length,&spec[startIndex],tnsFilter,temp);      /* Filter */
        }
    }
#ifdef FAAC_TNS_TUNING
    if (tnsInfo->tnsDataPresent)
        tns_applied_count++;
#endif
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

    if (tnsInfo->tnsDisabled) {
        tnsInfo->tnsDataPresent = 0;
        return;
    }
    int startBand,stopBand;    /* Bands over which to apply TNS */
    int w;

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

        if (tnsInfo->tnsDataPresent  &&  windowData->numFilters) {  /* Use TNS */
            int startIndex = w * windowSize + sfbOffsetTable[startBand];
            int length = sfbOffsetTable[stopBand] - sfbOffsetTable[startBand];
            TnsInvFilter(length,&spec[startIndex],tnsFilter,temp);
        }
    }
}




/********************************************************/
/* TnsInvFilter:                                        */
/*   Inverse filter the given spec with specified       */
/*   length using the coefficients specified in filter. */
/*   Note that the order and direction are specified    */
/*   within the TNS_FILTER_DATA structure.              */
/********************************************************/
static void TnsInvFilter(int length, faac_real * restrict spec,
                         const TnsFilterData * restrict filter,
                         faac_real * restrict temp)
{
    int i, j;
    const int order = filter->order;
    const faac_real * restrict a = filter->aCoeffs;

    if (filter->direction) {
        /* Backward direction (high-to-low index) */
        temp[length-1] = spec[length-1];
        for (i = length-2; i > (length-1-order); i--) {
            faac_real acc = spec[i];
            temp[i] = acc;
            for (j = 1; j <= (length-1-i); j++)
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
        /* Forward direction (low-to-high index) */
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

    /* Quantize and inverse quantize */
    for (i=1;i<=fOrder;i++) {
        indexArray[i] = (kArray[i]>=0)?(int)(0.5+(FAAC_ASIN(kArray[i])*iqfac)):(int)(-0.5+(FAAC_ASIN(kArray[i])*iqfac_m));
        kArray[i] = FAAC_SIN((faac_real)indexArray[i]/((indexArray[i]>=0)?iqfac:iqfac_m));
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

    /* Hoist maxOrder clamping for bulk of the loop for L1 locality */
    int limit = dataSize - maxOrder;
    for (index = 0; index < limit; index++) {
        const faac_real d = data[index];
        const faac_real * restrict dp = &data[index + 1];
        rArray[0] += d * d;
        for (order = 1; order <= maxOrder; order++)
            rArray[order] += d * dp[order - 1];
    }

    for (; index < dataSize; index++) {
        const faac_real d = data[index];
        int n = dataSize - 1 - index;
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
static faac_real LevinsonDurbin(int fOrder,          /* Filter order */
                      int dataSize,        /* Size of the data array */
                      const faac_real * restrict data,        /* Data array */
                      faac_real * restrict kArray)      /* Reflection coeff array */
{
    int order,i;
    faac_real signal;
    faac_real error, kTemp;                /* Prediction error */
    faac_real aArray1[TNS_MAX_ORDER+1];    /* Predictor coeff array */
    faac_real aArray2[TNS_MAX_ORDER+1];    /* Predictor coeff array 2 */
    faac_real rArray[TNS_MAX_ORDER+1] = {0}; /* Autocorrelation coeffs */
    faac_real* aPtr = aArray1;             /* Ptr to aArray1 */
    faac_real* aLastPtr = aArray2;         /* Ptr to aArray2 */
    faac_real* aTemp;

    /* Compute autocorrelation coefficients */
    Autocorrelation(fOrder,dataSize,data,rArray);
    signal=rArray[0];   /* signal energy */

    /* Set up pointers to current and last iteration */
    /* predictor coefficients.                       */
    aPtr = aArray1;
    aLastPtr = aArray2;
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
        /* If perfect prediction, trigger TNS */
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
