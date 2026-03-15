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

/***********************************************/
/* TNS Profile/Frequency Dependent Parameters  */
/***********************************************/
static unsigned short tnsMinBandNumberLong[12] = { 11, 12, 15, 16, 17, 20, 25, 26, 24, 28, 30, 31 };
static unsigned short tnsMinBandNumberShort[12] = { 2, 2, 2, 3, 3, 4, 6, 6, 8, 10, 10, 12 };
static unsigned short tnsMaxBandsLongMainLow[12] = { 31, 31, 34, 40, 42, 51, 46, 46, 42, 42, 42, 39 };
static unsigned short tnsMaxBandsShortMainLow[12] = { 9, 9, 10, 14, 14, 14, 14, 14, 14, 14, 14, 14 };

static void Autocorrelation(int maxOrder, int dataSize, const faac_real* __restrict data, faac_real* __restrict rArray);
static faac_real LevinsonDurbin(int fOrder, const faac_real* __restrict rArray, faac_real* __restrict kArray);
static void StepUp(int fOrder, const faac_real* __restrict kArray, faac_real* __restrict aArray);
static void QuantizeReflectionCoeffs(int fOrder, int coeffRes, faac_real* __restrict kArray, int* __restrict indexArray);
static int TruncateCoeffs(int fOrder, faac_real threshold, faac_real* __restrict kArray);
static void TnsInvFilter(int length, faac_real* __restrict spec, TnsFilterData* filter, faac_real* __restrict temp);

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
               int* sfbOffsetTable, faac_real* spec, faac_real* temp)
{
    int numberOfWindows, windowSize;
    int startBand, stopBand, order;
    int w;
    int startIndex, length;
    faac_real gain;
    faac_real tnsThresh;

    if (blockType == ONLY_SHORT_WINDOW) {
        numberOfWindows = MAX_SHORT_WINDOWS;
        windowSize = BLOCK_LEN_SHORT;
        startBand = tnsInfo->tnsMinBandNumberShort;
        stopBand = min(numberOfBands, tnsInfo->tnsMaxBandsShort);
        order = tnsInfo->tnsMaxOrderShort;
        tnsThresh = DEF_TNS_GAIN_THRESH_SHORT;
    } else {
        numberOfWindows = 1;
        windowSize = BLOCK_LEN_LONG;
        startBand = tnsInfo->tnsMinBandNumberLong;
        stopBand = min(numberOfBands, tnsInfo->tnsMaxBandsLong);
        order = tnsInfo->tnsMaxOrderLong;
        tnsThresh = DEF_TNS_GAIN_THRESH;
    }

    startBand = max(0, min(startBand, maxSfb));
    stopBand = max(0, min(stopBand, maxSfb));

    tnsInfo->tnsDataPresent = 0;

    for (w = 0; w < numberOfWindows; w++) {
        TnsWindowData* windowData = &tnsInfo->windowData[w];
        windowData->numFilters = 0;
        windowData->coefResolution = DEF_TNS_COEFF_RES;

        startIndex = w * windowSize + sfbOffsetTable[startBand];
        length = sfbOffsetTable[stopBand] - sfbOffsetTable[startBand];

        if (length <= 0) continue;

        faac_real rArray[TNS_MAX_ORDER + 1] = {0.0};
        faac_real k[TNS_MAX_ORDER + 1];

        Autocorrelation(order, length, &spec[startIndex], rArray);

        /* Fast Gating: Skip if energy is near noise floor or highly uncorrelated */
        if (rArray[0] < 1e-4 || FAAC_FABS(rArray[1]) < 0.15 * rArray[0]) continue;

        gain = LevinsonDurbin(order, rArray, k);

        if (gain > tnsThresh) {
            TnsFilterData* tnsFilter = &windowData->tnsFilter[0];
            windowData->numFilters = 1;
            tnsInfo->tnsDataPresent = 1;
            tnsFilter->direction = 0;
            tnsFilter->coefCompress = 0;
            tnsFilter->length = stopBand - startBand;
            QuantizeReflectionCoeffs(order, DEF_TNS_COEFF_RES, k, tnsFilter->index);
            tnsFilter->order = TruncateCoeffs(order, DEF_TNS_COEFF_THRESH, k);
            StepUp(tnsFilter->order, k, tnsFilter->aCoeffs);
            TnsInvFilter(length, &spec[startIndex], tnsFilter, temp);
        }
    }
}

void TnsEncodeFilterOnly(TnsInfo* tnsInfo, int numberOfBands, int maxSfb, enum WINDOW_TYPE blockType,
                         int* sfbOffsetTable, faac_real* spec, faac_real* temp)
{
    int numberOfWindows, windowSize;
    int startBand, stopBand;
    int w;
    int startIndex, length;

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

static void TnsInvFilter(int length, faac_real* __restrict spec, TnsFilterData* filter, faac_real* __restrict temp)
{
    int i, j;
    const int order = filter->order;
    const faac_real* __restrict a = filter->aCoeffs;

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

static int TruncateCoeffs(int fOrder, faac_real threshold, faac_real* __restrict kArray)
{
    int i;
    for (i = fOrder; i >= 1; i--) {
        if (FAAC_FABS(kArray[i]) > threshold) return i;
    }
    return 0;
}

static void QuantizeReflectionCoeffs(int fOrder, int coeffRes, faac_real* __restrict kArray, int* __restrict indexArray)
{
    const faac_real iqfac = ((1 << (coeffRes - 1)) - 0.5) / (M_PI / 2.0);
    int i;
    for (i = 1; i <= fOrder; i++) {
        const faac_real ki = kArray[i];
        indexArray[i] = (int)(FAAC_ASIN(ki) * iqfac + (ki >= 0 ? 0.5 : -0.5));
        kArray[i] = FAAC_SIN((faac_real)indexArray[i] / iqfac);
    }
}

static void Autocorrelation(int maxOrder, int dataSize, const faac_real* __restrict data, faac_real* __restrict rArray)
{
    int i, j;
    for (j = 0; j <= maxOrder; j++) {
        faac_real sum = 0.0;
        for (i = 0; i < dataSize - j; i++) {
            sum += data[i] * data[i + j];
        }
        rArray[j] = sum;
    }
}

static faac_real LevinsonDurbin(int fOrder, const faac_real* __restrict rArray, faac_real* __restrict kArray)
{
    int order, i;
    faac_real signal = rArray[0];
    faac_real error, kTemp;
    faac_real aArray1[TNS_MAX_ORDER + 1];
    faac_real aArray2[TNS_MAX_ORDER + 1];
    faac_real* __restrict aPtr = aArray1;
    faac_real* __restrict aLastPtr = aArray2;

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

        /* Stability guard: strict limit to prevent filter artifacts */
        if (kTemp > 0.90) kTemp = 0.90;
        if (kTemp < -0.90) kTemp = -0.90;

        kArray[order] = kTemp;
        aPtr[order] = kTemp;
        for (i = 1; i < order; i++) {
            aPtr[i] = aLastPtr[i] + kTemp * aLastPtr[order - i];
        }
        error *= (1.0 - kTemp * kTemp);

        faac_real* __restrict aTemp = aLastPtr;
        aLastPtr = aPtr;
        aPtr = aTemp;
    }

    return signal / error;
}

static void StepUp(int fOrder, const faac_real* __restrict kArray, faac_real* __restrict aArray)
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
