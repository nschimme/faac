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
#include <string.h>
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


/*************************/
/* Function prototypes   */
/*************************/
static void Autocorrelation(const int maxOrder,
                     const int dataSize,
                     const faac_real* __restrict data,
                     faac_real* __restrict rArray);

static faac_real LevinsonDurbin(const int fOrder, const faac_real* __restrict rArray, faac_real* __restrict kArray);

static void StepUp(const int fOrder, const faac_real* __restrict kArray, faac_real* __restrict aArray);

static void QuantizeReflectionCoeffs(const int fOrder, const int coeffRes, faac_real* __restrict kArray, int* __restrict indexArray);
static int TruncateCoeffs(const int fOrder, const faac_real threshold, faac_real* __restrict kArray);
static void TnsInvFilter(int length, faac_real* __restrict spec, TnsFilterData* filter, faac_real* __restrict temp);


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
            if (hEncoder->config.mpegVersion == 1) { /* MPEG2 */
                tnsInfo->tnsMaxOrderLong = tnsMaxOrderLongLow;
            } else { /* MPEG4 */
                if (fsIndex <= 5) /* fs > 32000Hz */
                    tnsInfo->tnsMaxOrderLong = 12;
                else
                    tnsInfo->tnsMaxOrderLong = 20;
            }
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
void TnsEncode(TnsInfo* tnsInfo,
               int numberOfBands,
               int maxSfb,
               enum WINDOW_TYPE blockType,
               int* sfbOffsetTable,
               faac_real* spec,
               faac_real* temp,
               faac_real tnsGainThresh)
{
    int numberOfWindows, windowSize;
    int startBand, stopBand, order;
    int lengthInBands;
    int w;
    int startOffset, stopOffset, length;

    switch( blockType ) {
    case ONLY_SHORT_WINDOW :
        numberOfWindows = MAX_SHORT_WINDOWS;
        windowSize = BLOCK_LEN_SHORT;
        startBand = tnsInfo->tnsMinBandNumberShort;
        stopBand = min(numberOfBands, tnsInfo->tnsMaxBandsShort);
        order = tnsInfo->tnsMaxOrderShort;
        break;

    default:
        numberOfWindows = 1;
        windowSize = BLOCK_LEN_LONG;
        startBand = tnsInfo->tnsMinBandNumberLong;
        stopBand = min(numberOfBands, tnsInfo->tnsMaxBandsLong);
        order = tnsInfo->tnsMaxOrderLong;
        break;
    }

    /* Make sure that start and stop bands < maxSfb */
    startBand = max(0, min(startBand, maxSfb));
    stopBand = max(0, min(stopBand, maxSfb));
    lengthInBands = stopBand - startBand;

    startOffset = sfbOffsetTable[startBand];
    stopOffset = sfbOffsetTable[stopBand];
    length = stopOffset - startOffset;

    tnsInfo->tnsDataPresent = 0;

    if (length <= 0) return;

    /* Perform analysis and filtering for each window */
    for (w = 0; w < numberOfWindows; w++) {
        TnsWindowData* windowData = &tnsInfo->windowData[w];
        TnsFilterData* tnsFilter = windowData->tnsFilter;
        faac_real* __restrict k = tnsFilter->kCoeffs;
        faac_real* __restrict a = tnsFilter->aCoeffs;
        faac_real* __restrict pSpec = &spec[w * windowSize + startOffset];
        faac_real rArray[TNS_MAX_ORDER + 1];
        faac_real gain;

        windowData->numFilters = 0;
        windowData->coefResolution = DEF_TNS_COEFF_RES;

        Autocorrelation(order, length, pSpec, rArray);

        /* Energy and Correlation-based Gating */
        if (rArray[0] < 1e-9 || FAAC_FABS(rArray[1]) < 0.05 * rArray[0]) {
            continue;
        }

        gain = LevinsonDurbin(order, rArray, k);

        if (gain > tnsGainThresh) {  /* Use TNS */
            int truncatedOrder;
            windowData->numFilters++;
            tnsInfo->tnsDataPresent = 1;
            tnsFilter->direction = 0;
            tnsFilter->coefCompress = 0;
            tnsFilter->length = lengthInBands;
            QuantizeReflectionCoeffs(order, DEF_TNS_COEFF_RES, k, tnsFilter->index);
            truncatedOrder = TruncateCoeffs(order, DEF_TNS_COEFF_THRESH, k);
            tnsFilter->order = truncatedOrder;
            StepUp(truncatedOrder, k, a);
            TnsInvFilter(length, pSpec, tnsFilter, temp);
        }
    }
}


/*****************************************************/
/* TnsEncodeFilterOnly:                              */
/*****************************************************/
void TnsEncodeFilterOnly(TnsInfo* tnsInfo,
                         int numberOfBands,
                         int maxSfb,
                         enum WINDOW_TYPE blockType,
                         int* sfbOffsetTable,
                         faac_real* spec,
                         faac_real* temp)
{
    int numberOfWindows, windowSize;
    int startBand, stopBand;
    int w;
    int startOffset, length;

    switch( blockType ) {
    case ONLY_SHORT_WINDOW :
        numberOfWindows = MAX_SHORT_WINDOWS;
        windowSize = BLOCK_LEN_SHORT;
        startBand = tnsInfo->tnsMinBandNumberShort;
        stopBand = min(numberOfBands, tnsInfo->tnsMaxBandsShort);
        break;

    default:
        numberOfWindows = 1;
        windowSize = BLOCK_LEN_LONG;
        startBand = tnsInfo->tnsMinBandNumberLong;
        stopBand = min(numberOfBands, tnsInfo->tnsMaxBandsLong);
        break;
    }

    startBand = max(0, min(startBand, maxSfb));
    stopBand = max(0, min(stopBand, maxSfb));

    startOffset = sfbOffsetTable[startBand];
    length = sfbOffsetTable[stopBand] - startOffset;

    if (length <= 0) return;

    for (w = 0; w < numberOfWindows; w++)
    {
        TnsWindowData* windowData = &tnsInfo->windowData[w];
        if (tnsInfo->tnsDataPresent && windowData->numFilters) {
            TnsInvFilter(length, &spec[w * windowSize + startOffset], windowData->tnsFilter, temp);
        }
    }
}


/********************************************************/
/* TnsInvFilter:                                        */
/********************************************************/
static void TnsInvFilter(int length, faac_real* __restrict spec, TnsFilterData* filter, faac_real* __restrict temp)
{
    int i, j;
    const int order = filter->order;
    const faac_real* __restrict a = filter->aCoeffs;

    if (length <= 0 || order <= 0) return;

    /* Separating input and output to enable better auto-vectorization */
    memcpy(temp, spec, length * sizeof(faac_real));

    if (filter->direction) {
        /* Backward FIR Filter: Parallel-friendly lag-outer approach */
        for (i = 0; i < length; i++) spec[i] = temp[i];
        for (j = 1; j <= order; j++) {
            const faac_real aj = a[j];
            const int n = length - j;
            for (i = 0; i < n; i++) {
                spec[i] += aj * temp[i + j];
            }
        }
    } else {
        /* Forward FIR Filter: Parallel-friendly lag-outer approach */
        for (i = 0; i < length; i++) spec[i] = temp[i];
        for (j = 1; j <= order; j++) {
            const faac_real aj = a[j];
            for (i = j; i < length; i++) {
                spec[i] += aj * temp[i - j];
            }
        }
    }
}


/*****************************************************/
/* TruncateCoeffs:                                   */
/*****************************************************/
static int TruncateCoeffs(const int fOrder, const faac_real threshold, faac_real* __restrict kArray)
{
    int i;
    for (i = fOrder; i >= 1; i--) {
        if (FAAC_FABS(kArray[i]) > threshold) return i;
        kArray[i] = 0.0;
    }
    return 0;
}

/*****************************************************/
/* QuantizeReflectionCoeffs:                         */
/*****************************************************/
static void QuantizeReflectionCoeffs(const int fOrder,
                              const int coeffRes,
                              faac_real* __restrict kArray,
                              int* __restrict indexArray)
{
    const faac_real iqfac = ((1 << (coeffRes - 1)) - 0.5) / (M_PI / 2);
    const faac_real iqfac_m = ((1 << (coeffRes - 1)) + 0.5) / (M_PI / 2);
    int i;

    for (i = 1; i <= fOrder; i++) {
        const faac_real ki = kArray[i];
        if (ki >= 0) {
            indexArray[i] = (int)(0.5 + (FAAC_ASIN(ki) * iqfac));
            kArray[i] = FAAC_SIN((faac_real)indexArray[i] / iqfac);
        } else {
            indexArray[i] = (int)(-0.5 + (FAAC_ASIN(ki) * iqfac_m));
            kArray[i] = FAAC_SIN((faac_real)indexArray[i] / iqfac_m);
        }
    }
}

/*****************************************************/
/* Autocorrelation,                                  */
/*****************************************************/
static void Autocorrelation(const int maxOrder,
                     const int dataSize,
                     const faac_real* __restrict data,
                     faac_real* __restrict rArray)
{
    int i, j;
    for (i = 0; i <= maxOrder; i++) rArray[i] = 0.0;
    if (dataSize <= 0) return;

    /* Accessing 'data' once and updating all lags to improve L1 cache locality */
    for (i = 0; i < dataSize; i++) {
        const faac_real d = data[i];
        const int limit = min(maxOrder, dataSize - 1 - i);
        for (j = 0; j <= limit; j++) {
            rArray[j] += d * data[i + j];
        }
    }
}


/*****************************************************/
/* LevinsonDurbin:                                   */
/*****************************************************/
static faac_real LevinsonDurbin(const int fOrder, const faac_real* __restrict rArray, faac_real* __restrict kArray)
{
    int order, i;
    faac_real signal = rArray[0];
    faac_real error, kTemp;
    faac_real aArray1[TNS_MAX_ORDER+1];
    faac_real aArray2[TNS_MAX_ORDER+1];
    faac_real* __restrict aPtr = aArray1;
    faac_real* __restrict aLastPtr = aArray2;

    if (signal <= 0.0) {
        kArray[0] = 1.0;
        for (order = 1; order <= fOrder; order++) kArray[order] = 0.0;
        return 0;
    }

    kArray[0] = 1.0;
    aPtr[0] = 1.0;
    aLastPtr[0] = 1.0;
    error = signal;

    for (order = 1; order <= fOrder; order++) {
        kTemp = aLastPtr[0] * rArray[order];
        for (i = 1; i < order; i++) kTemp += aLastPtr[i] * rArray[order - i];

        kTemp = -kTemp / error;
        kArray[order] = kTemp;
        aPtr[order] = kTemp;
        for (i = 1; i < order; i++) aPtr[i] = aLastPtr[i] + kTemp * aLastPtr[order - i];

        error *= (1.0 - kTemp * kTemp);

        faac_real* __restrict aTemp = aLastPtr;
        aLastPtr = aPtr;
        aPtr = aTemp;
    }
    return signal / error;
}


/*****************************************************/
/* StepUp:                                           */
/*   Convert reflection coefficients into            */
/*   predictor coefficients.                         */
/*****************************************************/
static void StepUp(const int fOrder, const faac_real* __restrict kArray, faac_real* __restrict aArray)
{
    faac_real aTemp[TNS_MAX_ORDER+2];
    int i, order;

    aArray[0] = 1.0;
    aTemp[0] = 1.0;
    for (order = 1; order <= fOrder; order++) {
        for (i = 1; i < order; i++) aTemp[i] = aArray[i] + kArray[order] * aArray[order - i];
        aTemp[order] = kArray[order];
        for (i = 1; i <= order; i++) aArray[i] = aTemp[i];
    }
}
