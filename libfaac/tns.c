/**********************************************************************
 * FAAC TNS implementation - ISO/IEC 14496-3 Section 4.6.8
 * v20: Bitrate-adaptive thresholds with pointer-optimized filters.
 **********************************************************************/

#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "frame.h"
#include "coder.h"
#include "bitstream.h"
#include "tns.h"
#include "util.h"

/* Standard band limits per Section 4.6.8.1 */
static unsigned short tnsMinBandNumberLong[12] =
{ 11, 12, 15, 16, 17, 20, 25, 26, 24, 28, 30, 31 };
static unsigned short tnsMinBandNumberShort[12] =
{ 2, 2, 2, 3, 3, 4, 6, 6, 8, 10, 10, 12 };

static unsigned short tnsMaxBandsLongMainLow[12] =
{ 31, 31, 34, 40, 42, 51, 46, 46, 42, 42, 42, 39 };

static unsigned short tnsMaxBandsShortMainLow[12] =
{ 9, 9, 10, 14, 14, 14, 14, 14, 14, 14, 14, 14 };

static void Autocorrelation(int maxOrder, int dataSize, faac_real* data, faac_real* rArray);
static faac_real LevinsonDurbin(int maxOrder, int dataSize, faac_real* data, faac_real* kArray);
static void StepUp(int fOrder, faac_real* kArray, faac_real* aArray);
static void QuantizeReflectionCoeffs(int fOrder, int coeffRes, faac_real* rArray, int* indexArray);
static int TruncateCoeffs(int fOrder, faac_real threshold, faac_real* kArray);
static void TnsInvFilter(int length, faac_real* spec, TnsFilterData* filter);

void TnsInit(faacEncStruct* hEncoder)
{
    unsigned int channel;
    int fsIndex = hEncoder->sampleRateIdx;
    int profile = hEncoder->config.aacObjectType;

    for (channel = 0; channel < hEncoder->numChannels; channel++) {
        TnsInfo *tnsInfo = &hEncoder->coderInfo[channel].tnsInfo;
        tnsInfo->tnsMaxOrderLong = (profile == MAIN || profile == LTP) ? 20 : 12;
        tnsInfo->tnsMaxOrderShort = 7;
        tnsInfo->tnsMaxBandsLong = tnsMaxBandsLongMainLow[fsIndex];
        tnsInfo->tnsMaxBandsShort = tnsMaxBandsShortMainLow[fsIndex];
        tnsInfo->tnsMinBandNumberLong = tnsMinBandNumberLong[fsIndex];
        tnsInfo->tnsMinBandNumberShort = tnsMinBandNumberShort[fsIndex];
    }
}

void TnsEncode(TnsInfo* tnsInfo, int numberOfBands, int maxSfb, enum WINDOW_TYPE blockType,
               int* sfbOffsetTable, faac_real* spec, int bitRate)
{
    int numberOfWindows, windowSize, startBand, stopBand, order;
    int w, startIndex, length;
    faac_real gain;

    switch(blockType) {
    case ONLY_SHORT_WINDOW:
        numberOfWindows = MAX_SHORT_WINDOWS;
        windowSize = BLOCK_LEN_SHORT;
        startBand = tnsInfo->tnsMinBandNumberShort;
        stopBand = min(numberOfBands, tnsInfo->tnsMaxBandsShort);
        /* DEVIATION: Cap order at 4 for short windows to save bits. */
        order = 4;
        break;
    default:
        numberOfWindows = 1;
        windowSize = BLOCK_LEN_LONG;
        startBand = tnsInfo->tnsMinBandNumberLong;
        stopBand = min(numberOfBands, tnsInfo->tnsMaxBandsLong);
        /* DEVIATION: Cap order at 12 for long windows to preserve bits at lower bitrates. */
        order = (bitRate > 0 && bitRate < 48000) ? 8 : min(tnsInfo->tnsMaxOrderLong, 12);
        break;
    }

    startBand = min(startBand, maxSfb);
    stopBand = min(stopBand, maxSfb);
    tnsInfo->tnsDataPresent = 0;

    /* DEVIATION: Conservative adaptive thresholds.
       TNS activation depends on bitrate to prevent quantizer starvation.
       v20: Tighter thresholds for mid-bitrate speech to avoid echo regressions. */
    faac_real threshold = 2.0;
    if (bitRate > 0) {
        if (bitRate < 16000) threshold = 6.0;
        else if (bitRate < 32000) threshold = 4.0;
        else if (bitRate < 48000) threshold = 3.5;
        else if (bitRate < 64000) threshold = 3.0;
    }

    /* Short windows are more sensitive to bit consumption. */
    if (blockType == ONLY_SHORT_WINDOW) threshold += 1.0;

    for (w = 0; w < numberOfWindows; w++) {
        TnsWindowData* windowData = &tnsInfo->windowData[w];
        TnsFilterData* tnsFilter = windowData->tnsFilter;
        faac_real* k = tnsFilter->kCoeffs;
        faac_real* a = tnsFilter->aCoeffs;

        windowData->numFilters = 0;
        windowData->coefResolution = DEF_TNS_COEFF_RES;
        startIndex = w * windowSize + sfbOffsetTable[startBand];
        length = sfbOffsetTable[stopBand] - sfbOffsetTable[startBand];

        if (length <= order) continue;

        gain = LevinsonDurbin(order, length, &spec[startIndex], k);

        if (gain > threshold) {
            int truncatedOrder;
            faac_real k_orig[TNS_MAX_ORDER+1];
            memcpy(k_orig, k, (order + 1) * sizeof(faac_real));

            windowData->numFilters++;
            tnsInfo->tnsDataPresent = 1;
            tnsFilter->direction = 0;
            tnsFilter->length = stopBand - startBand;

            QuantizeReflectionCoeffs(order, DEF_TNS_COEFF_RES, k, tnsFilter->index);
            truncatedOrder = TruncateCoeffs(order, DEF_TNS_COEFF_THRESH, k);
            tnsFilter->order = truncatedOrder;

            /* ISO/IEC 14496-3 Section 4.6.8.3: Coefficient Compression (3-bit) */
            tnsFilter->coefCompress = 0;
            if (truncatedOrder > 0) {
                int i, canCompress = 1;
                for (i = 1; i <= truncatedOrder; i++) {
                    if (tnsFilter->index[i] > 3 || tnsFilter->index[i] < -4) {
                        canCompress = 0;
                        break;
                    }
                }
                if (canCompress) {
                    tnsFilter->coefCompress = 1;
                    /* Re-quantize original coefficients using 3-bit resolution */
                    QuantizeReflectionCoeffs(truncatedOrder, DEF_TNS_COEFF_RES - 1, k_orig, tnsFilter->index);
                    memcpy(k, k_orig, (truncatedOrder + 1) * sizeof(faac_real));
                }
            }

            StepUp(truncatedOrder, k, a);
            TnsInvFilter(length, &spec[startIndex], tnsFilter);
        }
    }
}

void TnsEncodeFilterOnly(TnsInfo* tnsInfo, int numberOfBands, int maxSfb,
                         enum WINDOW_TYPE blockType, int *sfbOffsetTable, faac_real *spec)
{
    int numberOfWindows, windowSize, startBand, stopBand, w, startIndex, length;
    switch(blockType) {
    case ONLY_SHORT_WINDOW:
        numberOfWindows = MAX_SHORT_WINDOWS;
        windowSize = BLOCK_LEN_SHORT;
        startBand = min(tnsInfo->tnsMinBandNumberShort, maxSfb);
        stopBand = min(numberOfBands, maxSfb);
        break;
    default:
        numberOfWindows = 1;
        windowSize = BLOCK_LEN_LONG;
        startBand = min(tnsInfo->tnsMinBandNumberLong, maxSfb);
        stopBand = min(numberOfBands, maxSfb);
        break;
    }

    for(w=0; w<numberOfWindows; w++) {
        TnsWindowData* windowData = &tnsInfo->windowData[w];
        if (tnsInfo->tnsDataPresent && windowData->numFilters) {
            startIndex = w * windowSize + sfbOffsetTable[startBand];
            length = sfbOffsetTable[stopBand] - sfbOffsetTable[startBand];
            TnsInvFilter(length, &spec[startIndex], windowData->tnsFilter);
        }
    }
}

/* FIR Filter for Encoding - Optimized with pointers */
static void TnsInvFilter(int length, faac_real* spec, TnsFilterData* filter)
{
    int i, j, order = filter->order;
    faac_real* a = filter->aCoeffs;
    faac_real temp[BLOCK_LEN_LONG];
    if (length <= 0) return;
    if (length > BLOCK_LEN_LONG) length = BLOCK_LEN_LONG;
    memcpy(temp, spec, length * sizeof(faac_real));

    if (filter->direction) {
        faac_real *pspec = spec + length - 1;
        for (i = length - 1; i >= 0; i--) {
            int k = (order < length - 1 - i) ? order : (length - 1 - i);
            faac_real sum = 0;
            faac_real *pa = a + 1;
            faac_real *pt = temp + i + 1;
            for (j = 1; j <= k; j++) sum += (*pt++) * (*pa++);
            *pspec-- += sum;
        }
    } else {
        faac_real *pspec = spec;
        for (i = 0; i < length; i++) {
            int k = (order < i) ? order : i;
            faac_real sum = 0;
            faac_real *pa = a + 1;
            faac_real *pt = temp + i - 1;
            for (j = 1; j <= k; j++) sum += (*pt--) * (*pa++);
            *pspec++ += sum;
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

static void QuantizeReflectionCoeffs(int fOrder, int coeffRes, faac_real* rArray, int* indexArray)
{
    faac_real iqfac = ((1 << (coeffRes - 1)) - 0.5) / (M_PI / 2);
    faac_real iqfac_m = ((1 << (coeffRes - 1)) + 0.5) / (M_PI / 2);
    int i, maxIndex = (1 << (coeffRes - 1)) - 1, minIndex = -(1 << (coeffRes - 1));
    for (i = 1; i <= fOrder; i++) {
        int index = (rArray[i] >= 0) ? (int)(0.5 + (FAAC_ASIN(rArray[i]) * iqfac)) : (int)(-0.5 + (FAAC_ASIN(rArray[i]) * iqfac_m));
        if (index > maxIndex) index = maxIndex;
        if (index < minIndex) index = minIndex;
        indexArray[i] = index;
        rArray[i] = FAAC_SIN((faac_real)index / ((index >= 0) ? iqfac : iqfac_m));
    }
}

/* Autocorrelation optimized with pointers */
static void Autocorrelation(int maxOrder, int dataSize, faac_real* data, faac_real* rArray)
{
    int order, index;
    for (order = 0; order <= maxOrder; order++) {
        faac_real sum = 0.0;
        int limit = dataSize - order;
        faac_real *p1 = data, *p2 = data + order;
        for (index = 0; index < limit; index++) sum += (*p1++) * (*p2++);
        rArray[order] = sum;
    }
}

static faac_real LevinsonDurbin(int fOrder, int dataSize, faac_real* data, faac_real* kArray)
{
    int order, i;
    faac_real signal, error, kTemp, aArray1[TNS_MAX_ORDER+1], aArray2[TNS_MAX_ORDER+1], rArray[TNS_MAX_ORDER+1] = {0};
    faac_real *aPtr = aArray1, *aLastPtr = aArray2, *aTemp;
    Autocorrelation(fOrder, dataSize, data, rArray);
    signal = rArray[0];
    if (signal < 1e-9) {
        for (order = 0; order <= fOrder; order++) kArray[order] = (order == 0) ? 1.0 : 0.0;
        return 0;
    }
    kArray[0] = 1.0; aPtr[0] = 1.0; aLastPtr[0] = 1.0; error = rArray[0];
    for (order = 1; order <= fOrder; order++) {
        kTemp = aLastPtr[0] * rArray[order];
        for (i = 1; i < order; i++) kTemp += aLastPtr[i] * rArray[order-i];
        kTemp = -kTemp / error;
        kArray[order] = kTemp; aPtr[order] = kTemp;
        for (i = 1; i < order; i++) aPtr[i] = aLastPtr[i] + kTemp * aLastPtr[order-i];
        error *= (1 - kTemp * kTemp);
        aTemp = aLastPtr; aLastPtr = aPtr; aPtr = aTemp;
    }
    return signal / error;
}

static void StepUp(int fOrder, faac_real* kArray, faac_real* aArray)
{
    faac_real aTemp[TNS_MAX_ORDER+2];
    int i, order;
    aArray[0] = 1.0; aTemp[0] = 1.0;
    for (order = 1; order <= fOrder; order++) {
        aArray[order] = 0.0;
        for (i = 1; i <= order; i++) aTemp[i] = aArray[i] + kArray[order] * aArray[order-i];
        for (i = 1; i <= order; i++) aArray[i] = aTemp[i];
    }
}
