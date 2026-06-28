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

#include <math.h>
#include <string.h>
#include "frame.h"
#include "coder.h"
#include "bitstream.h"
#include "tns.h"
#include "util.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/***********************************************/
/* TNS Profile/Frequency Dependent Parameters  */
/***********************************************/
/* Limit bands to > 2.0 kHz (Approximate) */
static unsigned short tnsMinBandNumberLong[12] =
{ 11, 12, 15, 12, 12, 15, 16, 17, 20, 25, 26, 24 };
static unsigned short tnsMinBandNumberShort[12] =
{ 2, 2, 2, 2, 2, 3, 3, 4, 6, 6, 8, 10 };

/**************************************/
/* Low Profile TNS Parameters         */
/**************************************/
/* Aligned with ISO/IEC 14496-3:2005 Tables 4.113 - 4.117 */
static unsigned short tnsMaxBandsLongLow[12] =
{ 31, 31, 31, 46, 46, 34, 40, 42, 51, 46, 46, 39 };

static unsigned short tnsMaxBandsShortLow[12] =
{ 9, 9, 9, 14, 14, 10, 14, 14, 14, 14, 14, 14 };

static unsigned short tnsMaxOrderLongLow  = 12;
static unsigned short tnsMaxOrderShortLow = 7;

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
static int TruncateCoeffs(int fOrder,faac_real threshold,faac_real* kArray, int* indexArray);
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

    if (fsIndex > 11) fsIndex = 11;

    for (channel = 0; channel < hEncoder->numChannels; channel++) {
        TnsInfo *tnsInfo = &hEncoder->coderInfo[channel].tnsInfo;

        tnsInfo->tnsMaxBandsLong       = tnsMaxBandsLongLow[fsIndex];
        tnsInfo->tnsMaxBandsShort      = tnsMaxBandsShortLow[fsIndex];
        tnsInfo->tnsMaxOrderLong       = tnsMaxOrderLongLow;
        tnsInfo->tnsMaxOrderShort      = tnsMaxOrderShortLow;
        tnsInfo->tnsMinBandNumberLong  = tnsMinBandNumberLong[fsIndex];
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
    int startBand,stopBand,maxOrder;    /* Bands over which to apply TNS */
    int w, i;
    int startIndex,length;
    faac_real gain;

    if (blockType == ONLY_SHORT_WINDOW) {
        numberOfWindows = 8;
        windowSize = BLOCK_LEN_SHORT;
        startBand = tnsInfo->tnsMinBandNumberShort;
        stopBand = min(numberOfBands, tnsInfo->tnsMaxBandsShort);
        maxOrder = tnsInfo->tnsMaxOrderShort;
    } else {
        numberOfWindows = 1;
        windowSize = BLOCK_LEN_LONG;
        startBand = tnsInfo->tnsMinBandNumberLong;
        stopBand = min(numberOfBands, tnsInfo->tnsMaxBandsLong);
        maxOrder = tnsInfo->tnsMaxOrderLong;
    }

    /* Make sure that start and stop bands < maxSfb */
    startBand = min(startBand, maxSfb);
    stopBand = min(stopBand, maxSfb);
    startBand = max(startBand, 0);
    stopBand = max(stopBand, 0);

    tnsInfo->tnsDataPresent = 0;     /* default TNS not used */

    /* Perform analysis and filtering for each window */
    for (w=0;w<numberOfWindows;w++) {

        TnsWindowData* windowData = &tnsInfo->windowData[w];
        TnsFilterData* tnsFilter = windowData->tnsFilter;
        faac_real* k = tnsFilter->kCoeffs;    /* reflection coeffs */
        faac_real* a = tnsFilter->aCoeffs;    /* prediction coeffs */

        windowData->numFilters=0;
        windowData->coefResolution = (blockType == ONLY_SHORT_WINDOW) ? 3 : 4;
        startIndex = w * windowSize + sfbOffsetTable[startBand];
        length = sfbOffsetTable[stopBand] - sfbOffsetTable[startBand];

        if (length <= 0) continue;

        gain = LevinsonDurbin(maxOrder,length,&spec[startIndex],k);

        /* TNS activation threshold. Spec-compliant encoders use ~1.4. */
        if (gain > 1.1) {  /* Use TNS */
            int truncatedOrder;
            QuantizeReflectionCoeffs(maxOrder,windowData->coefResolution,k,tnsFilter->index);
            truncatedOrder = TruncateCoeffs(maxOrder,DEF_TNS_COEFF_THRESH,k,tnsFilter->index);
            if (truncatedOrder == 0) continue;

            windowData->numFilters = 1;
            tnsInfo->tnsDataPresent = 1;

            /* Direction logic: check energy slant */
            double low_e = 0, high_e = 0;
            int mid = length / 2;
            for (i = 0; i < mid; i++) low_e += (double)spec[startIndex + i] * spec[startIndex + i];
            for (i = mid; i < length; i++) high_e += (double)spec[startIndex + i] * spec[startIndex + i];
            tnsFilter->direction = (high_e > low_e) ? 1 : 0;

            /* Coeff compression logic: indices 0..max_q can be compressed as MSB is 0 */
            tnsFilter->coefCompress = 1;
            int max_q = (windowData->coefResolution == 4) ? 7 : 3;
            for (i = 1; i <= truncatedOrder; i++) {
                int idx = tnsFilter->index[i];
                if (idx > max_q) {
                    tnsFilter->coefCompress = 0;
                    break;
                }
            }

            tnsFilter->length = stopBand - startBand;
            tnsFilter->order = truncatedOrder;
            StepUp(truncatedOrder,k,a);    /* Compute predictor coefficients */
            TnsInvFilter(length,&spec[startIndex],tnsFilter,temp);      /* Filter */
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
        /* Now filter the rest */
        for (i = order; i < length; i++) {
            faac_real acc = spec[i];
            temp[i] = acc;
            for (j = 1; j <= order; j++)
                acc += temp[i-j] * a[j];
            spec[i] = acc;
        }
    }
}

static int TruncateCoeffs(int fOrder,faac_real threshold,faac_real* kArray, int* indexArray)
{
    int i;
    for (i = fOrder; i >= 1; i--) {
        if (FAAC_FABS(kArray[i]) > threshold) {
             return i;
        }
        kArray[i] = 0.0;
        indexArray[i] = 0;
    }
    return 0;
}

/*
 * QuantizeReflectionCoeffs:
 * Implementation of ISO/IEC 14496-3 Table 4.83
 * Index mapping:
 * idx = 0 -> 0.0
 * idx 1..max_q -> sin(-idx * pi / divisor)
 * idx mask..mask-max_q+1 -> sin((mask+1-idx) * pi / divisor)
 */
static void QuantizeReflectionCoeffs(int fOrder,
                              int coeffRes,
                              faac_real* kArray,
                              int* indexArray)
{
    int i;
    double pi = M_PI;
    double divisor = (coeffRes == 4) ? 15.0 : 7.0;
    int max_q = (coeffRes == 4) ? 7 : 3;
    int mask = (1 << coeffRes) - 1;

    for (i = 1; i <= fOrder; i++) {
        double k = (double)kArray[i];
        int q;

        if (k > 0.99) k = 0.99;
        if (k < -0.99) k = -0.99;

        q = (int)round(FAAC_ASIN(k) * divisor / pi);
        if (q > max_q) q = max_q;
        if (q < -max_q) q = -max_q;

        if (q == 0) {
            indexArray[i] = 0;
        } else if (q < 0) {
            indexArray[i] = -q;
        } else {
            indexArray[i] = mask + 1 - q;
        }

        /* Inverse quantize for StepUp */
        int idx = indexArray[i];
        if (idx == 0) {
            kArray[i] = 0.0;
        } else if (idx <= max_q) {
            kArray[i] = (faac_real)FAAC_SIN(-idx * pi / divisor);
        } else {
            kArray[i] = (faac_real)FAAC_SIN((mask + 1 - idx) * pi / divisor);
        }
    }
}

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
    faac_real rArray[TNS_MAX_ORDER+1] = {0};
    faac_real* aPtr = aArray1;
    faac_real* aLastPtr = aArray2;
    faac_real* aTemp;

    Autocorrelation(fOrder,dataSize,data,rArray);
    signal=rArray[0];

    if (signal <= 1e-6) {
        for (order=0;order<=fOrder;order++) kArray[order]=0.0;
        return 0;
    } else {
        aPtr[0]=1.0;
        aLastPtr[0]=1.0;
        error=rArray[0];

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
            error = error * (1.0 - kTemp*kTemp);
            if (error <= 0.0) break;

            aTemp=aLastPtr;
            aLastPtr=aPtr;
            aPtr=aTemp;
        }
        if (error <= 1e-9) return 100.0;
        return signal/error;
    }
}

static void StepUp(int fOrder,faac_real* kArray,faac_real* aArray)
{
    faac_real aTemp[TNS_MAX_ORDER+2];
    int i,order;

    for (i=0; i<=TNS_MAX_ORDER; i++) aArray[i] = 0.0;
    aArray[0]=1.0;
    aTemp[0]=1.0;
    for (order=1;order<=fOrder;order++) {
        for (i=1;i<=order;i++) {
            aTemp[i] = aArray[i] + kArray[order]*aArray[order-i];
        }
        for (i=1;i<=order;i++) {
            aArray[i]=aTemp[i];
        }
    }
}
