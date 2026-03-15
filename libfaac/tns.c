/**********************************************************************
TNS implementation for FAAC.
Based on original ISO/IEC reference code.
**********************************************************************/

#include <math.h>
#include <string.h>
#include "frame.h"
#include "coder.h"
#include "bitstream.h"
#include "tns.h"
#include "util.h"

static unsigned short tnsMinBandNumberLong[12] = { 11, 12, 15, 16, 17, 20, 25, 26, 24, 28, 30, 31 };
static unsigned short tnsMinBandNumberShort[12] = { 2, 2, 2, 3, 3, 4, 6, 6, 8, 10, 10, 12 };
static unsigned short tnsMaxBandsLongMainLow[12] = { 31, 31, 34, 40, 42, 51, 46, 46, 42, 42, 42, 39 };
static unsigned short tnsMaxBandsShortMainLow[12] = { 9, 9, 10, 14, 14, 14, 14, 14, 14, 14, 14, 14 };

static void Autocorrelation(int maxOrder, int dataSize, faac_real* data, faac_real* rArray);
static faac_real LevinsonDurbin(int fOrder, int dataSize, faac_real* data, faac_real* kArray);
static void StepUp(int fOrder, faac_real* kArray, faac_real* aArray);
static void QuantizeReflectionCoeffs(int fOrder, int coeffRes, faac_real* kArray, int* indexArray);
static int TruncateCoeffs(int fOrder, faac_real threshold, faac_real* kArray);
static void TnsInvFilter(int length, faac_real* spec, TnsFilterData* filter, faac_real *temp);

void TnsInit(faacEncStruct* hEncoder)
{
    unsigned int channel;
    int fsIndex = hEncoder->sampleRateIdx;

    for (channel = 0; channel < hEncoder->numChannels; channel++) {
        TnsInfo *tnsInfo = &hEncoder->coderInfo[channel].tnsInfo;
        tnsInfo->tnsMaxBandsLong = tnsMaxBandsLongMainLow[fsIndex];
        tnsInfo->tnsMaxBandsShort = tnsMaxBandsShortMainLow[fsIndex];
        tnsInfo->tnsMaxOrderLong = 12;
        tnsInfo->tnsMaxOrderShort = 7;
        tnsInfo->tnsMinBandNumberLong = tnsMinBandNumberLong[fsIndex];
        tnsInfo->tnsMinBandNumberShort = tnsMinBandNumberShort[fsIndex];
    }
}

void TnsEncode(TnsInfo* tnsInfo, int numberOfBands, int maxSfb, enum WINDOW_TYPE blockType,
               int* sfbOffsetTable, faac_real* spec, faac_real* temp, faac_real tnsGainThresh)
{
    int numberOfWindows, windowSize, startBand, stopBand, order, w, startIndex, length;
    faac_real gain;

    if (blockType == ONLY_SHORT_WINDOW) {
        numberOfWindows = MAX_SHORT_WINDOWS;
        windowSize = BLOCK_LEN_SHORT;
        startBand = tnsInfo->tnsMinBandNumberShort;
        stopBand = min(numberOfBands, tnsInfo->tnsMaxBandsShort);
        order = tnsInfo->tnsMaxOrderShort;
    } else {
        numberOfWindows = 1;
        windowSize = BLOCK_LEN_LONG;
        startBand = tnsInfo->tnsMinBandNumberLong;
        stopBand = min(numberOfBands, tnsInfo->tnsMaxBandsLong);
        order = tnsInfo->tnsMaxOrderLong;
    }

    startBand = max(0, min(startBand, maxSfb));
    stopBand = max(0, min(stopBand, maxSfb));

    tnsInfo->tnsDataPresent = 0;

    for (w = 0; w < numberOfWindows; w++) {
        TnsWindowData* windowData = &tnsInfo->windowData[w];
        TnsFilterData* tnsFilter = &windowData->tnsFilter[0];

        windowData->numFilters = 0;
        windowData->coefResolution = DEF_TNS_COEFF_RES;

        startIndex = w * windowSize + sfbOffsetTable[startBand];
        length = sfbOffsetTable[stopBand] - sfbOffsetTable[startBand];

        if (length <= 0) continue;

        /* Skip if energy is near noise floor */
        faac_real en = 0;
        int i;
        for (i = 0; i < length; i++) en += spec[startIndex+i] * spec[startIndex+i];
        if (en < 1e-4) continue;

        gain = LevinsonDurbin(order, length, &spec[startIndex], tnsFilter->kCoeffs);

        if (gain > tnsGainThresh) {
            windowData->numFilters = 1;
            tnsInfo->tnsDataPresent = 1;
            tnsFilter->direction = 0;
            tnsFilter->coefCompress = 0;
            tnsFilter->length = stopBand - startBand;
            QuantizeReflectionCoeffs(order, DEF_TNS_COEFF_RES, tnsFilter->kCoeffs, tnsFilter->index);
            tnsFilter->order = TruncateCoeffs(order, DEF_TNS_COEFF_THRESH, tnsFilter->kCoeffs);
            StepUp(tnsFilter->order, tnsFilter->kCoeffs, tnsFilter->aCoeffs);
            TnsInvFilter(length, &spec[startIndex], tnsFilter, temp);
        }
    }
}

void TnsEncodeFilterOnly(TnsInfo* tnsInfo, int numberOfBands, int maxSfb, enum WINDOW_TYPE blockType,
                         int* sfbOffsetTable, faac_real* spec, faac_real* temp)
{
    int numberOfWindows, windowSize, startBand, stopBand, w, startIndex, length;

    if (blockType == ONLY_SHORT_WINDOW) {
        numberOfWindows = MAX_SHORT_WINDOWS;
        windowSize = BLOCK_LEN_SHORT;
        startBand = tnsInfo->tnsMinBandNumberShort;
        stopBand = min(numberOfBands, tnsInfo->tnsMaxBandsShort);
    } else {
        numberOfWindows = 1;
        windowSize = BLOCK_LEN_LONG;
        startBand = tnsInfo->tnsMinBandNumberLong;
        stopBand = min(numberOfBands, tnsInfo->tnsMaxBandsLong);
    }

    startBand = max(0, min(startBand, maxSfb));
    stopBand = max(0, min(stopBand, maxSfb));

    if (!tnsInfo->tnsDataPresent) return;

    for (w = 0; w < numberOfWindows; w++) {
        TnsWindowData* windowData = &tnsInfo->windowData[w];
        TnsFilterData* tnsFilter = &windowData->tnsFilter[0];

        startIndex = w * windowSize + sfbOffsetTable[startBand];
        length = sfbOffsetTable[stopBand] - sfbOffsetTable[startBand];

        if (windowData->numFilters && length > 0) {
            TnsInvFilter(length, &spec[startIndex], tnsFilter, temp);
        }
    }
}

static void TnsInvFilter(int length, faac_real* spec, TnsFilterData* filter, faac_real *temp)
{
    int i, j;
    int order = filter->order;
    faac_real* a = filter->aCoeffs;

    if (length <= 0 || order <= 0) return;

    memcpy(temp, spec, length * sizeof(faac_real));

    for (i = 0; i < length; i++) {
        for (j = 1; j <= order; j++) {
            if (i >= j) {
                spec[i] += temp[i - j] * a[j];
            }
        }
    }
}

static int TruncateCoeffs(int fOrder, faac_real threshold, faac_real* kArray)
{
    int i;
    for (i = fOrder; i >= 1; i--) {
        if (FAAC_FABS(kArray[i]) > threshold) return i;
    }
    return 0;
}

static void QuantizeReflectionCoeffs(int fOrder, int coeffRes, faac_real* kArray, int* indexArray)
{
    faac_real iqfac = ((1 << (coeffRes - 1)) - 0.5) / (M_PI / 2);
    int i;
    for (i = 1; i <= fOrder; i++) {
        indexArray[i] = (int)(FAAC_ASIN(kArray[i]) * iqfac + (kArray[i] >= 0 ? 0.5 : -0.5));
        kArray[i] = FAAC_SIN((faac_real)indexArray[i] / iqfac);
    }
}

static void Autocorrelation(int maxOrder, int dataSize, faac_real* data, faac_real* rArray)
{
    int order, index;
    for (order = 0; order <= maxOrder; order++) {
        rArray[order] = 0.0;
        for (index = 0; index < dataSize - order; index++) {
            rArray[order] += data[index] * data[index + order];
        }
    }
}

static faac_real LevinsonDurbin(int fOrder, int dataSize, faac_real* data, faac_real* kArray)
{
    int order, i;
    faac_real signal, error, kTemp;
    faac_real aArray1[TNS_MAX_ORDER + 1], aArray2[TNS_MAX_ORDER + 1];
    faac_real rArray[TNS_MAX_ORDER + 1] = { 0.0 };
    faac_real *aPtr = aArray1, *aLastPtr = aArray2, *aTemp;

    Autocorrelation(fOrder, dataSize, data, rArray);

    signal = rArray[0];
    if (signal < 1e-9) return 0.0;

    kArray[0] = 1.0;
    aPtr[0] = 1.0;
    aLastPtr[0] = 1.0;
    error = signal;

    for (order = 1; order <= fOrder; order++) {
        kTemp = 0;
        for (i = 0; i < order; i++) {
            kTemp += aLastPtr[i] * rArray[order - i];
        }
        kTemp = -kTemp / error;

        /* Stability guard: limit reflection coefficients to prevent ringing */
        if (kTemp > 0.85) kTemp = 0.85;
        if (kTemp < -0.85) kTemp = -0.85;

        kArray[order] = kTemp;
        aPtr[order] = kTemp;
        for (i = 1; i < order; i++) {
            aPtr[i] = aLastPtr[i] + kTemp * aLastPtr[order - i];
        }
        error *= (1.0 - kTemp * kTemp);

        aTemp = aLastPtr;
        aLastPtr = aPtr;
        aPtr = aTemp;
    }

    return signal / error;
}

static void StepUp(int fOrder, faac_real* kArray, faac_real* aArray)
{
    faac_real aTemp[TNS_MAX_ORDER + 2];
    int i, order;
    aArray[0] = 1.0;
    aTemp[0] = 1.0;
    for (order = 1; order <= fOrder; order++) {
        for (i = 1; i < order; i++) {
            aTemp[i] = aArray[i] + kArray[order] * aArray[order - i];
        }
        aTemp[order] = kArray[order];
        for (i = 1; i <= order; i++) {
            aArray[i] = aTemp[i];
        }
    }
}
