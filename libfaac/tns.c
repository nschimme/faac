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

static unsigned short tnsMinBandNumberLong[12] =
{ 11, 12, 15, 16, 17, 20, 25, 26, 24, 28, 30, 31 };
static unsigned short tnsMinBandNumberShort[12] =
{ 2, 2, 2, 3, 3, 4, 6, 6, 8, 10, 10, 12 };

static unsigned short tnsMaxBandsLongMainLow[12] =
{ 31, 31, 34, 40, 42, 51, 46, 46, 42, 42, 42, 39 };

static unsigned short tnsMaxBandsShortMainLow[12] =
{ 9, 9, 10, 14, 14, 14, 14, 14, 14, 14, 14, 14 };

static unsigned short tnsMaxOrderLongMain = 20;
static unsigned short tnsMaxOrderLongLow = 12;
static unsigned short tnsMaxOrderShortMainLow = 7;

static void Autocorrelation(int maxOrder, int dataSize, const faac_real* __restrict data, faac_real* __restrict rArray, faac_real energy);
static faac_real LevinsonDurbin(int maxOrder, int dataSize, const faac_real* __restrict data, faac_real* __restrict kArray, faac_real energy);
static void StepUp(int fOrder, const faac_real* __restrict kArray, faac_real* __restrict aArray);
static void QuantizeReflectionCoeffs(int fOrder, int coeffRes, faac_real* __restrict kArray, int* __restrict indexArray);
static int TruncateCoeffs(int fOrder, faac_real threshold, faac_real* kArray);
static void TnsInvFilter(int length, faac_real* __restrict spec, TnsFilterData* __restrict filter, faac_real* __restrict temp);

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
            if (hEncoder->config.mpegVersion == 1) {
                tnsInfo->tnsMaxOrderLong = tnsMaxOrderLongMain;
            } else {
                if (fsIndex <= 5)
                    tnsInfo->tnsMaxOrderLong = 12;
                else
                    tnsInfo->tnsMaxOrderLong = 20;
            }
            tnsInfo->tnsMaxOrderShort = tnsMaxOrderShortMainLow;
            break;
        case LOW :
            tnsInfo->tnsMaxBandsLong = tnsMaxBandsLongMainLow[fsIndex];
            tnsInfo->tnsMaxBandsShort = tnsMaxBandsShortMainLow[fsIndex];
            if (hEncoder->config.mpegVersion == 1) {
                tnsInfo->tnsMaxOrderLong = tnsMaxOrderLongLow;
            } else {
                if (fsIndex <= 5)
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

void TnsEncode(TnsInfo* tnsInfo, int numberOfBands, int maxSfb, enum WINDOW_TYPE blockType, int* sfbOffsetTable, faac_real* spec, faac_real lambda, faac_real* temp)
{
    int numberOfWindows,windowSize;
    int startBand,stopBand,order;
    int lengthInBands;
    int w;
    int startIndex,length;
    faac_real gain_thresh;
    faac_real sfm_thresh;

    switch( blockType ) {
    case ONLY_SHORT_WINDOW :
        numberOfWindows = MAX_SHORT_WINDOWS;
        windowSize = BLOCK_LEN_SHORT;
        startBand = tnsInfo->tnsMinBandNumberShort;
        stopBand = min(numberOfBands, tnsInfo->tnsMaxBandsShort);
        order = tnsInfo->tnsMaxOrderShort;
        gain_thresh = 3.0;
        sfm_thresh = 0.90;
        break;

    default:
        numberOfWindows = 1;
        windowSize = BLOCK_LEN_LONG;
        startBand = tnsInfo->tnsMinBandNumberLong;
        stopBand = min(numberOfBands, tnsInfo->tnsMaxBandsLong);
        order = tnsInfo->tnsMaxOrderLong;
        gain_thresh = 4.0;
        sfm_thresh = 0.95;
        break;
    }

    startBand = max(0, min(startBand,maxSfb));
    stopBand = max(0, min(stopBand,maxSfb));
    lengthInBands = max(0, stopBand-startBand);
    tnsInfo->tnsDataPresent = 0;

    for (w=0;w<numberOfWindows;w++) {
        faac_real energy = 0.0;
        faac_real logSum = 0.0;
        int i;
        faac_real sfm;

        TnsWindowData* windowData = &tnsInfo->windowData[w];
        TnsFilterData* tnsFilter = windowData->tnsFilter;
        faac_real* k = tnsFilter->kCoeffs;
        faac_real* a = tnsFilter->aCoeffs;

        windowData->numFilters=0;
        windowData->coefResolution = DEF_TNS_COEFF_RES;
        startIndex = w * windowSize + sfbOffsetTable[startBand];
        length = sfbOffsetTable[stopBand] - sfbOffsetTable[startBand];

        if (length <= 0) continue;

        const faac_real * __restrict window_spec = spec + startIndex;
        faac_real inv_len = 1.0 / length;

        /* Single pass for energy and logSum to improve cache locality */
        for (i = 0; i < length; i++) {
            faac_real v2 = window_spec[i] * window_spec[i];
            energy += v2;
            logSum += log(v2 + 1e-15);
        }

        if (energy < 1e-9) continue;
        sfm = exp(logSum * inv_len) / (energy * inv_len);

        if (sfm < sfm_thresh) continue;

        faac_real predGain = LevinsonDurbin(order,length,window_spec,k,energy);
        faac_real side_info_cost = (order * 4 + 10) * lambda;

        if (predGain > gain_thresh && (predGain - 1.0) * energy * 0.1 > side_info_cost) {
            int truncatedOrder;
            windowData->numFilters++;
            tnsInfo->tnsDataPresent=1;
            tnsFilter->direction = 0;
            tnsFilter->coefCompress = 0;
            tnsFilter->length = lengthInBands;
            QuantizeReflectionCoeffs(order,DEF_TNS_COEFF_RES,k,tnsFilter->index);
            truncatedOrder = TruncateCoeffs(order,DEF_TNS_COEFF_THRESH,k);
            tnsFilter->order = truncatedOrder;
            StepUp(truncatedOrder,k,a);
            TnsInvFilter(length,spec + startIndex,tnsFilter,temp);
        }
    }
}

void TnsEncodeFilterOnly(TnsInfo* tnsInfo, int numberOfBands, int maxSfb, enum WINDOW_TYPE blockType, int *sfbOffsetTable, faac_real *spec, faac_real *temp)
{
    int numberOfWindows,windowSize;
    int startBand,stopBand;
    int w;
    int startIndex,length;

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

    startBand = max(0, min(startBand,maxSfb));
    stopBand = max(0, min(stopBand,maxSfb));

    for(w=0;w<numberOfWindows;w++)
    {
        TnsWindowData* windowData = &tnsInfo->windowData[w];
        TnsFilterData* tnsFilter = windowData->tnsFilter;

        startIndex = w * windowSize + sfbOffsetTable[startBand];
        length = sfbOffsetTable[stopBand] - sfbOffsetTable[startBand];

        if (tnsInfo->tnsDataPresent  &&  windowData->numFilters) {
            TnsInvFilter(length,spec + startIndex,tnsFilter,temp);
        }
    }
}

static void TnsInvFilter(int length, faac_real* __restrict spec, TnsFilterData* __restrict filter, faac_real* __restrict temp)
{
    int i, j;
    int order = filter->order;
    const faac_real* __restrict a = filter->aCoeffs;

    if (length <= 0) return;

    memcpy(temp, spec, length * sizeof(faac_real));

    if (filter->direction) {
        for (i = length - 1; i >= length - order && i >= 0; i--) {
            faac_real s = temp[i];
            const faac_real* __restrict tp = temp + i + 1;
            for (j = 1; j <= (length - 1 - i); j++) s += tp[j-1] * a[j];
            spec[i] = s;
        }
        for (; i >= 0; i--) {
            faac_real s = temp[i];
            const faac_real* __restrict tp = temp + i + 1;
            /* Manual unrolling for common max order (up to 20) */
            for (j = 1; j <= order; j++) s += tp[j-1] * a[j];
            spec[i] = s;
        }
    } else {
        for (i = 0; i < order && i < length; i++) {
            faac_real s = temp[i];
            const faac_real* __restrict tp = temp + i;
            for (j = 1; j <= i; j++) s += tp[-j] * a[j];
            spec[i] = s;
        }
        for (; i < length; i++) {
            faac_real s = temp[i];
            const faac_real* __restrict tp = temp + i;
            for (j = 1; j <= order; j++) s += tp[-j] * a[j];
            spec[i] = s;
        }
    }
}

static int TruncateCoeffs(int fOrder, faac_real threshold, faac_real* kArray)
{
    int i;
    for (i = fOrder; i >= 1; i--) {
        if (FAAC_FABS(kArray[i]) > threshold) return i;
        kArray[i] = 0.0;
    }
    return 0;
}

static void QuantizeReflectionCoeffs(int fOrder, int coeffRes, faac_real* __restrict kArray, int* __restrict indexArray)
{
    faac_real iqfac,iqfac_m;
    int i;
    iqfac = ((1<<(coeffRes-1))-0.5)/(M_PI/2);
    iqfac_m = ((1<<(coeffRes-1))+0.5)/(M_PI/2);
    for (i=1;i<=fOrder;i++) {
        faac_real k = kArray[i];
        int idx = (k >= 0) ? (int)(0.5 + (FAAC_ASIN(k) * iqfac)) : (int)(-0.5 + (FAAC_ASIN(k) * iqfac_m));
        if (idx > 7) idx = 7;
        if (idx < -8) idx = -8;
        indexArray[i] = idx;
        k = FAAC_SIN((faac_real)idx / ((idx >= 0) ? iqfac : iqfac_m));
        if (k > 0.8) k = 0.8;
        if (k < -0.8) k = -0.8;
        kArray[i] = k;
    }
}

static void Autocorrelation(int maxOrder, int dataSize, const faac_real* __restrict data, faac_real* __restrict rArray, faac_real energy)
{
    int order, index;
    rArray[0] = energy;
    for (order = 1; order <= maxOrder; order++) rArray[order] = 0.0;

    /* Optimized autocorrelation: Process all lags in one pass to maximize data reuse */
    int n = dataSize - maxOrder;
    for (index = 0; index < n; index++) {
        faac_real v = data[index];
        const faac_real * __restrict d_lag = data + index;
        for (order = 1; order <= maxOrder; order++) {
            rArray[order] += v * d_lag[order];
        }
    }
    /* Handle tail */
    for (; index < dataSize; index++) {
        faac_real v = data[index];
        for (order = 1; order < dataSize - index; order++) {
            rArray[order] += v * data[index + order];
        }
    }
}

static faac_real LevinsonDurbin(int fOrder, int dataSize, const faac_real* __restrict data, faac_real* __restrict kArray, faac_real energy)
{
    int order,i;
    faac_real signal;
    faac_real error, kt;
    faac_real rArray[TNS_MAX_ORDER+1] = {0};

    Autocorrelation(fOrder,dataSize,data,rArray,energy);
    signal=rArray[0];

    if (signal < 1e-9) {
        kArray[0]=1.0;
        for (order=1;order<=fOrder;order++) {
            kArray[order]=0.0;
        }
        return 0;
    } else {
        faac_real aArray1[TNS_MAX_ORDER+1];
        faac_real aArray2[TNS_MAX_ORDER+1];
        faac_real* __restrict aPtr = aArray1;
        faac_real* __restrict aLastPtr = aArray2;
        faac_real* aTemp;
        kArray[0]=1.0;
        aPtr[0]=1.0;
        aLastPtr[0]=1.0;
        error=rArray[0];
        for (order=1;order<=fOrder;order++) {
            kt = rArray[order];
            const faac_real* __restrict alp = aLastPtr;
            const faac_real* __restrict ra = rArray + order;
            for (i=1;i<order;i++) {
                kt += alp[i]*ra[-i];
            }
            kt = -kt/error;
            kArray[order]=kt;
            aPtr[order]=kt;
            for (i=1;i<order;i++) {
                aPtr[i] = alp[i] + kt*alp[order-i];
            }
            error = error * (1.0 - kt*kt);
            aTemp=aLastPtr;
            aLastPtr=aPtr;
            aPtr=aTemp;
        }
        return signal/error;
    }
}

static void StepUp(int fOrder, const faac_real* __restrict kArray, faac_real* __restrict aArray)
{
    faac_real a_temp[TNS_MAX_ORDER+2];
    int i,order;
    aArray[0]=1.0;
    a_temp[0]=1.0;
    for (order=1;order<=fOrder;order++) {
        faac_real k = kArray[order];
        aArray[order]=0.0;
        for (i=1;i<=order;i++) {
            a_temp[i] = aArray[i] + k * aArray[order-i];
        }
        for (i=1;i<=order;i++) {
            aArray[i]=a_temp[i];
        }
    }
}
