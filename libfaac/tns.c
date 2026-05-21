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
/* Main/Low Profile TNS Parameters    */
/**************************************/
static unsigned short tnsMaxBandsLongMainLow[12] =
{ 31, 31, 34, 40, 42, 51, 46, 46, 42, 42, 42, 39 };

static unsigned short tnsMaxBandsShortMainLow[12] =
{ 9, 9, 10, 14, 14, 14, 14, 14, 14, 14, 14, 14 };

static unsigned short tnsMaxOrderLongMain = 20;
static unsigned short tnsMaxOrderLongLow = 12;
static unsigned short tnsMaxOrderShortMainLow = 7;

/* TNS analysis pre-gate thresholds: skip the expensive LevinsonDurbin
   analysis on frames where TNS provably cannot help, so the cost (and
   any over-firing MOS regressions) are avoided on the silence/noise/
   flat-spectrum content that defined prior failed attempts. */
#define TNS_ENERGY_FLOOR  0.16     /* per-sample MDCT energy floor; avoids wasting bits on silence */
#define TNS_GAIN_THRESH_LOW   1.4  /* Minimum gain to bother with TNS */
#define TNS_GAIN_THRESH_HIGH  12.0 /* Max gain; above this is likely steady
                                      tonal content where TNS harms bitrate
                                      efficiency more than it helps masking. */
#define TNS_FLATNESS_K    1.5      /* L2^2*N / L1^2 minimum; < K means
                                      the band is too close to flat for
                                      cross-frequency LPC to predict */
#define TNS_PEAK_RATIO_MARGIN 1.2  /* peak-to-mean tonality gate factor.
                                      White Gaussian noise over N bins has
                                      expected peak-to-mean ~sqrt(2 ln N);
                                      we skip TNS when max|X|/mean|X| is
                                      below MARGIN * sqrt(2 ln N). MARGIN
                                      ~1.2 sits just above the noise floor
                                      and well below tonal-content ratios. */


/*************************/
/* Function prototypes   */
/*************************/
static void Autocorrelation(int maxOrder,        /* Maximum autocorr order */
                     int dataSize,        /* Size of the data array */
                     faac_real* data,        /* Data array */
                     faac_real* rArray);     /* Autocorrelation array */

static faac_real LevinsonDurbin(int maxOrder,        /* Maximum filter order */
                      int dataSize,        /* Size of the data array */
                      faac_real* data,        /* Data array */
                      faac_real* kArray);     /* Reflection coeff array */

static void StepUp(int fOrder, faac_real* kArray, faac_real* aArray);

static void QuantizeReflectionCoeffs(int fOrder,int coeffRes,faac_real* rArray,int* indexArray);
static int TruncateCoeffs(int fOrder,faac_real threshold,faac_real* kArray);
static void TnsInvFilter(int length,faac_real* spec,TnsFilterData* filter, faac_real *temp);

static void WhitenSpectrumForTns(const faac_real *spec, faac_real *out,
                                 const int *sfbOffsetTable,
                                 const faac_real *sfbEnergy,
                                 int startBand, int stopBand,
                                 int startLine, int stopLine);


/*****************************************************/
/* InitTns:                                          */
/*****************************************************/
void TnsInit(faacEncStruct* hEncoder)
{
    unsigned int channel;
    int fsIndex = hEncoder->sampleRateIdx;
    int profile = hEncoder->config.aacObjectType;

    for (channel = 0; channel < hEncoder->numChannels; channel++) {
        TnsInfo *tnsInfo = &hEncoder->coderInfo[channel].tnsInfo;

        switch( profile ) {
        case MAIN:
        case LTP:
            tnsInfo->tnsMaxBandsLong = tnsMaxBandsLongMainLow[fsIndex];
            tnsInfo->tnsMaxBandsShort = tnsMaxBandsShortMainLow[fsIndex];
            if (hEncoder->config.mpegVersion == 1) { /* MPEG2 */
                tnsInfo->tnsMaxOrderLong = tnsMaxOrderLongMain;
            } else { /* MPEG4 */
                if (fsIndex <= 5) /* fs > 32000Hz */
                    tnsInfo->tnsMaxOrderLong = 12;
                else
                    tnsInfo->tnsMaxOrderLong = 20;
            }
            tnsInfo->tnsMaxOrderShort = tnsMaxOrderShortMainLow;
            break;
        case LOW :
            tnsInfo->tnsMaxBandsLong = tnsMaxBandsLongMainLow[fsIndex];
            tnsInfo->tnsMaxBandsShort = tnsMaxBandsShortMainLow[fsIndex];
            /* LC (LOW) profile caps long-block TNS filter order at 12
               regardless of sample rate or MPEG version. The previous
               MPEG4 branch raised the cap to 20 for fs<32kHz - that
               was inherited from the MAIN profile and is invalid for
               LC: standard decoders (ffmpeg, faad) reject any LC frame
               carrying tns_order > 12, treating the whole packet as
               corrupt. This was harmless while TNS was off by default
               but is the dominant cause of catastrophic per-file MOS
               drops on the voip 16 kHz scenario now that TNS is on. */
            tnsInfo->tnsMaxOrderLong = tnsMaxOrderLongLow;  /* = 12 */
            tnsInfo->tnsMaxOrderShort = tnsMaxOrderShortMainLow;
            break;
        }
        tnsInfo->tnsMinBandNumberLong = tnsMinBandNumberLong[fsIndex];
        tnsInfo->tnsMinBandNumberShort = tnsMinBandNumberShort[fsIndex];
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
    int length;
    faac_real gain;

    switch( blockType ) {
    case ONLY_SHORT_WINDOW :
        numberOfWindows = MAX_SHORT_WINDOWS;
        windowSize = BLOCK_LEN_SHORT;
        startBand = tnsInfo->tnsMinBandNumberShort;
        stopBand = numberOfBands;
        lengthInBands = stopBand - startBand;
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

    /* Make sure that start and stop bands < maxSfb */
    /* Make sure that start and stop bands >= 0 */
    startBand = min(startBand,maxSfb);
    stopBand = min(stopBand,maxSfb);
    startBand = max(startBand,0);
    stopBand = max(stopBand,0);

    tnsInfo->tnsDataPresent = 0;     /* default TNS not used */

    /* Perform analysis and filtering for each window */
    for (w=0;w<numberOfWindows;w++) {

        TnsWindowData* windowData = &tnsInfo->windowData[w];
        TnsFilterData* tnsFilter = windowData->tnsFilter;
        faac_real* k = tnsFilter->kCoeffs;    /* reflection coeffs */
        faac_real* a = tnsFilter->aCoeffs;    /* prediction coeffs */
        faac_real sfbEnergy[NSFB_LONG];       /* per-SFB sum of squares,
                                                 filled by the pre-gate and
                                                 consumed by the whitener */

        windowData->numFilters=0;
        windowData->coefResolution = DEF_TNS_COEFF_RES;

        length = sfbOffsetTable[stopBand] - sfbOffsetTable[startBand];

        /* Cheap pre-gate fused with per-SFB energy accumulation.
           Walks the TNS band once, SFB by SFB, building both the
           pre-gate statistics (sumsq, suma, maxa) and the per-SFB
           sum-of-squares the whitener needs. Skip if the band is
           essentially silent or the spectrum is nearly flat
           (L2^2*N / L1^2 < TNS_FLATNESS_K, bounded below by 1.0 at
           perfect flatness by Cauchy-Schwarz).
           winOffset accounts for the window position in the spec[]
           buffer: for long blocks (w=0, windowSize=1024) this is 0
           so behaviour is unchanged; for short blocks (windowSize=128)
           sfbOffsetTable gives per-window offsets 0-128 and we must
           add w*128 to reach the correct window. */
        {
            faac_real sumsq = 0.0, suma = 0.0, maxa = 0.0;
            int sfb;
            int winOffset = w * windowSize;
            for (sfb = startBand; sfb < stopBand; sfb++) {
                faac_real e = 0.0;
                int j;
                for (j = sfbOffsetTable[sfb]; j < sfbOffsetTable[sfb + 1]; j++) {
                    faac_real v = spec[winOffset + j];
                    faac_real va = FAAC_FABS(v);
                    e    += v * v;
                    suma += va;
                    if (va > maxa) maxa = va;
                }
                sfbEnergy[sfb] = e;
                sumsq += e;
            }
            {
                /* Length-aware peak-to-mean threshold: the noise floor
                   for max|X|/mean|X| scales as sqrt(2 ln N). */
                faac_real peak_thresh = TNS_PEAK_RATIO_MARGIN
                                        * (faac_real)sqrt(2.0 * log((double)length));
                if (sumsq < TNS_ENERGY_FLOOR * length
                    || suma <= 0.0
                    || sumsq * length < TNS_FLATNESS_K * suma * suma
                    || maxa * length < peak_thresh * suma) {
                    continue;
                }
            }
        }

        /* Run LD on the per-SFB-whitened spectrum, not the raw one,
           so prediction gain reflects within-band correlation rather
           than formant-peak structure across SFBs. The whitened buffer
           lives in `temp`; it is consumed by LevinsonDurbin here and
           is reused by TnsInvFilter later (after the decision is
           made and the coefficients are quantised), so the storage
           does not collide. See WhitenSpectrumForTns for rationale. */
        WhitenSpectrumForTns(&spec[w * windowSize], temp, sfbOffsetTable, sfbEnergy,
                             startBand, stopBand,
                             sfbOffsetTable[startBand], sfbOffsetTable[stopBand]);
        gain = LevinsonDurbin(order,length,&temp[sfbOffsetTable[startBand]],k);

        if (gain > TNS_GAIN_THRESH_LOW && gain < TNS_GAIN_THRESH_HIGH) {  /* Use TNS */
            int truncatedOrder;
            QuantizeReflectionCoeffs(order,DEF_TNS_COEFF_RES,k,tnsFilter->index);
            truncatedOrder = TruncateCoeffs(order,DEF_TNS_COEFF_THRESH,k);
            if (truncatedOrder == 0) {
                /* Identity filter after truncation - skip so we do
                   not consume tns_data syntax bits for a no-op. With
                   whitening in place the order-1 "noise-overlay"
                   case the previous truncatedOrder<2 gate caught
                   should no longer fire (LD on whitened input does
                   not see the formant-peak prediction), so the gate
                   is back to the original identity check.  */
                continue;
            }
            windowData->numFilters++;
            tnsInfo->tnsDataPresent=1;
            tnsFilter->direction = 0;
            tnsFilter->coefCompress = 0;
            tnsFilter->length = lengthInBands;
            tnsFilter->order = truncatedOrder;
            StepUp(truncatedOrder,k,a);    /* Compute predictor coefficients */
            TnsInvFilter(length,&spec[w * windowSize + sfbOffsetTable[startBand]],tnsFilter,temp);      /* Filter */
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
    int length;

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


        length = sfbOffsetTable[stopBand] - sfbOffsetTable[startBand];

        if (tnsInfo->tnsDataPresent  &&  windowData->numFilters) {  /* Use TNS */
            TnsInvFilter(length,&spec[w * windowSize + sfbOffsetTable[startBand]],tnsFilter,temp);
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
static void TnsInvFilter(int length,faac_real* spec,TnsFilterData* filter, faac_real *temp)
{
    int i,j,k=0;
    int order=filter->order;
    faac_real* a=filter->aCoeffs;

    /* Determine loop parameters for given direction */
    if (filter->direction) {

        /* Startup, initial state is zero */
        temp[length-1]=spec[length-1];
        for (i=length-2;i>(length-1-order);i--) {
            temp[i]=spec[i];
            k++;
            for (j=1;j<=k;j++) {
                spec[i]+=temp[i+j]*a[j];
            }
        }

        /* Now filter the rest */
        for (i=length-1-order;i>=0;i--) {
            temp[i]=spec[i];
            for (j=1;j<=order;j++) {
                spec[i]+=temp[i+j]*a[j];
            }
        }


    } else {

        /* Startup, initial state is zero */
        temp[0]=spec[0];
        for (i=1;i<order;i++) {
            temp[i]=spec[i];
            for (j=1;j<=i;j++) {
                spec[i]+=temp[i-j]*a[j];
            }
        }

        /* Now filter the rest */
        for (i=order;i<length;i++) {
            temp[i]=spec[i];
            for (j=1;j<=order;j++) {
                spec[i]+=temp[i-j]*a[j];
            }
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
       [-8, 7].  Without clamping, kArray[i] near ±1 maps to asin(±1)
       = ±π/2, iqfac*(π/2) = 7.5, and (int)(0.5+7.5) = 8 which wraps
       to -8 as a 4-bit signed value — encoder/decoder filter mismatch. */
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
static void Autocorrelation(int maxOrder,        /* Maximum autocorr order */
                     int dataSize,        /* Size of the data array */
                     faac_real* data,        /* Data array */
                     faac_real* rArray)      /* Autocorrelation array */
{
    int order,index;

    for (order=0;order<=maxOrder;order++) {
        rArray[order]=0.0;
        for (index=0;index<dataSize;index++) {
            rArray[order]+=data[index]*data[index+order];
        }
        dataSize--;
    }
}



/******************************************************************/
/* WhitenSpectrumForTns:                                          */
/*   Per-scale-factor-band energy normalisation of the MDCT       */
/*   spectrum, used as the LevinsonDurbin input for TNS analysis. */
/*                                                                */
/*   Running LD on the raw spectrum lets the formant peaks        */
/*   dominate the fit: on noise-overlaid speech, LD reports a     */
/*   high prediction gain because it "predicted" the formant      */
/*   pattern, even when the within-band signal is essentially     */
/*   white. TNS then reshapes quantisation noise around those     */
/*   formants and the noise modulation is audible (the dominant   */
/*   long-block TNS killer pattern in CI, all NOISE-tagged files  */
/*   at low bitrate).                                             */
/*                                                                */
/*   The fix is to divide each spectral line by sqrt(SFB energy)  */
/*   before LD, so prediction gain reflects within-SFB structure  */
/*   (which is what TNS can actually exploit perceptually) and    */
/*   not peak-driven inter-SFB structure. A 3-tap triangle        */
/*   smoother is applied to the per-line weight to soften the     */
/*   step discontinuities at SFB boundaries that LD would         */
/*   otherwise fit as spurious correlation. Both reference        */
/*   encoders (ffmpeg, libaacplus) document this whitening step;  */
/*   FAAC was running LD on the raw spectrum and accumulating     */
/*   the noise-modulation bug as a result.                        */
/*                                                                */
/*   Caller must size out[] to at least stopLine elements.        */
/******************************************************************/
static void WhitenSpectrumForTns(const faac_real *spec, faac_real *out,
                                 const int *sfbOffsetTable,
                                 const faac_real *sfbEnergy,
                                 int startBand, int stopBand,
                                 int startLine, int stopLine)
{
    faac_real invE[NSFB_LONG];
    int sfb, i;

    if (startBand >= stopBand || startLine >= stopLine)
        return;

    /* Step 1: per-SFB inverse sqrt(energy). Energies come pre-computed
       from the pre-gate loop so we do not walk the band a second time
       just to accumulate them. The 1e-30 floor keeps silent bands from
       producing a NaN weight; their contribution to LD is irrelevant
       in practice. FAAC_SQRT picks sqrtf vs sqrt by precision build,
       so single-precision builds do not pay for the double-precision
       sqrt path 30-51 times per long-block frame. */
    for (sfb = startBand; sfb < stopBand; sfb++) {
        invE[sfb] = (faac_real)1.0
                  / FAAC_SQRT(sfbEnergy[sfb] + (faac_real)1e-30);
    }

    /* Step 2: expand to per-line weight, piecewise constant by SFB. */
    {
        int next = sfbOffsetTable[startBand + 1];
        faac_real w = invE[startBand];
        sfb = startBand;
        for (i = startLine; i < stopLine; i++) {
            if (i >= next && sfb + 1 < stopBand) {
                sfb++;
                w = invE[sfb];
                next = sfbOffsetTable[sfb + 1];
            }
            out[i] = w;
        }
    }

    /* Step 3a: right-to-left half of the triangle smoother. */
    for (i = stopLine - 2; i >= startLine; i--)
        out[i] = (faac_real)0.5 * (out[i] + out[i + 1]);

    /* Step 3b: left-to-right half, fused with multiplication by the
       raw spectrum so we walk the band once and finish in-place. The
       running `prev` keeps the smoothed weight before the multiply
       contaminates out[]. */
    {
        faac_real prev = out[startLine];
        out[startLine] = prev * spec[startLine];
        for (i = startLine + 1; i < stopLine; i++) {
            faac_real curr = (faac_real)0.5 * (out[i] + prev);
            out[i] = curr * spec[i];
            prev = curr;
        }
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
                      faac_real* data,        /* Data array */
                      faac_real* kArray)      /* Reflection coeff array */
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

    /* Zero output reflection coefficients before doing any work. kArray
       is the caller's tnsFilter->kCoeffs, which is per-channel state
       persisted across frames; when the recursion below exits early
       (via the error<=0 or |kTemp|>=error guard, common on highly
       correlated input such as speech formants) the entries beyond the
       break index would otherwise retain stale values from a previous
       frame and be propagated into QuantizeReflectionCoeffs / StepUp /
       TnsInvFilter, producing a garbage prediction filter and severe
       noise modulation. (The kArray[0]=1.0 assignments in the branches
       below still set the sentinel correctly.) */
    for (order = 0; order <= fOrder; order++) {
        kArray[order] = 0.0;
    }

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
        if (error <= 0.0) return DEF_TNS_GAIN_THRESH + 1.0;
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
