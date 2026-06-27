/************************* MPEG-2 NBC Audio Decoder **************************
 *                                                                           *
"This software module was originally developed in the course of
development of the MPEG-2 NBC/MPEG-4 Audio standard ISO/IEC 13818-7,
14496-1,2 and 3. This software module is an implementation of a part of one or more
MPEG-2 NBC/MPEG-4 Audio tools as specified by the MPEG-2 NBC/MPEG-4
Audio standard. ISO/IEC  gives users of the MPEG-2 NBC/MPEG-4 Audio
standards free license to this software module or modifications thereof for use in
hardware or software products claiming conformance to the MPEG-2 NBC/MPEG-4
Audio  standards. Those intending to use this software module in hardware or
software products are advised that this use may infringe existing patents.
The original developer of this software module and his/her company, the subsequent
editors and their companies, and ISO/IEC have no liability for use of this software
module or modifications thereof in an implementation. Copyright is not released for
non MPEG-2 NBC/MPEG-4 Audio conforming products.The original developer
retains full right to use the code for his/her  own purpose, assign or donate the
code to a third party and to inhibit third party from using the code for non
MPEG-2 NBC/MPEG-4 Audio conforming products. This copyright notice must
be included in all copies or derivative works."
Copyright(c)1996.
 *                                                                           *
 ****************************************************************************/

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "frame.h"
#include "coder.h"
#include "bitstream.h"
#include "tns.h"
#include "util.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Spec-compliant TNS coefficient maps from ISO/IEC 14496-3 (Offset-based centered mapping) */
/* 4-bit resolution: k = sin((index - 8) * (pi/2) / 7.5) */
static const float tns_map_4[16] = {
    -0.9945218954f, -0.9945218954f, -0.9510565163f, -0.8660254038f,
    -0.7431448255f, -0.5877852523f, -0.4067366431f, -0.2079116908f,
     0.0000000000f,  0.2079116908f,  0.4067366431f,  0.5877852523f,
     0.7431448255f,  0.8660254038f,  0.9510565163f,  0.9945218954f
};

/* 3-bit resolution: k = sin((index - 4) * (pi/2) / 3.5) */
static const float tns_map_3[8] = {
    -0.9749279122f, -0.9749279122f, -0.7818314825f, -0.4338837391f,
     0.0000000000f,  0.4338837391f,  0.7818314825f,  0.9749279122f
};

static void Autocorrelation(int maxOrder, int dataSize, const faac_real * data, faac_real * rArray);
static int LevinsonDurbin(int maxOrder, int dataSize, const faac_real * data, faac_real * kArray, faac_real * gain);
static void StepUp(int fOrder, faac_real* kArray, faac_real* aArray);
static void QuantizeReflectionCoeffs(int fOrder, int coeffRes, faac_real* kArray, int* indexArray);
static void TnsFilter(int length, faac_real * spec, const TnsFilterData * filter, faac_real * temp);

/* TNS max bands based on ISO/IEC 14496-3 Table 4.84 and 4.85 */
static const int tnsMaxBandsLongTable[13] = {
    31, 31, 34, 40, 42, 51, 46, 46, 42, 42, 42, 40, 40
};
static const int tnsMaxBandsShortTable[13] = {
    9, 9, 10, 14, 14, 14, 15, 15, 15, 15, 15, 15, 15
};

/* TNS min band tables for sample rate dependency */
static const int tnsMinBandNumberLongTable[13] = { 11, 12, 15, 16, 17, 25, 26, 26, 26, 26, 26, 26, 26 };
static const int tnsMinBandNumberShortTable[13] = { 2, 2, 2, 3, 3, 4, 6, 6, 6, 6, 6, 6, 6 };

void TnsInit(faacEncStruct* hEncoder)
{
    int ch;
    int idx = hEncoder->sampleRateIdx;
    if (idx > 12) idx = 12;

    for (ch = 0; ch < hEncoder->numChannels; ch++) {
        TnsInfo *tns = &hEncoder->coderInfo[ch].tnsInfo;
        tns->tnsMaxBandsLong = tnsMaxBandsLongTable[idx];
        tns->tnsMaxBandsShort = tnsMaxBandsShortTable[idx];
        tns->tnsMinBandNumberLong = tnsMinBandNumberLongTable[idx];
        tns->tnsMinBandNumberShort = tnsMinBandNumberShortTable[idx];
        tns->tnsMaxOrderLong = 12;
        tns->tnsMaxOrderShort = 7;
    }
}

void TnsEncode(TnsInfo* tnsInfo, int numberOfBands, int maxSfb, enum WINDOW_TYPE blockType, int* sfbOffsetTable, faac_real* spec, faac_real* temp)
{
    int numberOfWindows, windowSize, startBand, order, maxBands;
    int w;
    faac_real gain, thresh;

    if (blockType == ONLY_SHORT_WINDOW) {
        numberOfWindows = 8;
        windowSize = 128;
        startBand = tnsInfo->tnsMinBandNumberShort;
        maxBands = tnsInfo->tnsMaxBandsShort;
        order = tnsInfo->tnsMaxOrderShort;
        thresh = 1.4;
    } else {
        numberOfWindows = 1;
        windowSize = 1024;
        startBand = tnsInfo->tnsMinBandNumberLong;
        maxBands = tnsInfo->tnsMaxBandsLong;
        order = tnsInfo->tnsMaxOrderLong;
        thresh = 1.6;
    }

    /* Important: Decoder hardcodes tns_max_bands based on sampling frequency.
       We must analyze up to this band for correct bitstream alignment. */
    maxBands = min(maxBands, numberOfBands);
    startBand = min(startBand, maxBands);

    tnsInfo->tnsDataPresent = 0;

    for (w = 0; w < numberOfWindows; w++) {
        TnsWindowData* windowData = &tnsInfo->windowData[w];
        TnsFilterData* tnsFilter = windowData->tnsFilter;

        memset(windowData, 0, sizeof(TnsWindowData));

        int windowOffset = w * windowSize;
        int startIndex = windowOffset + sfbOffsetTable[startBand];
        int lengthInLines = sfbOffsetTable[maxBands] - sfbOffsetTable[startBand];

        if (lengthInLines <= order) continue;

        int actualOrder = LevinsonDurbin(order, lengthInLines, &spec[startIndex], tnsFilter->kCoeffs, &gain);

        if (gain > thresh) {
            /* Energy slant heuristic for direction */
            faac_real e_low = 0, e_high = 0;
            int mid = lengthInLines / 2;
            int i;
            for (i = 0; i < mid; i++) e_low += spec[startIndex + i] * spec[startIndex + i];
            for (i = mid; i < lengthInLines; i++) e_high += spec[startIndex + i] * spec[startIndex + i];

            tnsFilter->direction = (e_high > e_low) ? 1 : 0;

            QuantizeReflectionCoeffs(actualOrder, 4, tnsFilter->kCoeffs, tnsFilter->index);
            windowData->numFilters = 1;
            windowData->coefResolution = 4;
            tnsInfo->tnsDataPresent = 1;
            tnsFilter->order = actualOrder;
            tnsFilter->length = maxBands - startBand;

            StepUp(actualOrder, tnsFilter->kCoeffs, tnsFilter->aCoeffs);
            TnsFilter(lengthInLines, &spec[startIndex], tnsFilter, temp);
        }
    }
}

static void TnsFilter(int length, faac_real * spec, const TnsFilterData * filter, faac_real * temp)
{
    int i, j;
    const int order = filter->order;
    const faac_real * a = filter->aCoeffs;

    if (order <= 0) return;

    /* Copy original spectral coefficients to temp for FIR (Moving Average) filtering.
       Sign convention: spec_filt[n] = original[n] + prediction[n] */
    memcpy(temp, spec, length * sizeof(faac_real));

    if (filter->direction == 0) { /* Forward */
        for (i = 0; i < length; i++) {
            faac_real acc = 0;
            for (j = 1; j <= order && j <= i; j++) {
                acc += a[j] * temp[i - j];
            }
            spec[i] += acc;
        }
    } else { /* Backward */
        for (i = length - 1; i >= 0; i--) {
            faac_real acc = 0;
            for (j = 1; j <= order && (i + j) < length; j++) {
                acc += a[j] * temp[i + j];
            }
            spec[i] += acc;
        }
    }
}

static void QuantizeReflectionCoeffs(int fOrder, int coeffRes, faac_real* kArray, int* indexArray)
{
    int i, j;
    const float* map = (coeffRes == 4) ? tns_map_4 : tns_map_3;
    int num_vals = (coeffRes == 4) ? 16 : 8;
    for (i = 1; i <= fOrder; i++) {
        float min_err = 1e10f;
        int best_idx = 0;
        for (j = 0; j < num_vals; j++) {
            float err = (float)FAAC_FABS(kArray[i] - (faac_real)map[j]);
            if (err < min_err) { min_err = err; best_idx = j; }
        }
        indexArray[i] = best_idx;
        kArray[i] = (faac_real)map[best_idx];
    }
}

static void Autocorrelation(int maxOrder, int dataSize, const faac_real * data, faac_real * rArray)
{
    int i, j;
    for (i = 0; i <= maxOrder; i++) {
        rArray[i] = 0;
        for (j = 0; j < dataSize - i; j++) {
            rArray[i] += data[j] * data[j + i];
        }
    }
}

static int LevinsonDurbin(int fOrder, int dataSize, const faac_real * data, faac_real * kArray, faac_real * gain)
{
    faac_real r[TNS_MAX_ORDER + 1];
    faac_real a[TNS_MAX_ORDER + 1][TNS_MAX_ORDER + 1];
    faac_real e;
    int i, j;

    memset(r, 0, sizeof(r));
    memset(a, 0, sizeof(a));
    memset(kArray, 0, (fOrder + 1) * sizeof(faac_real));

    Autocorrelation(fOrder, dataSize, data, r);
    if (r[0] < 1e-9) {
        *gain = 1.0;
        return 0;
    }

    e = r[0];
    for (i = 1; i <= fOrder; i++) {
        faac_real s = r[i];
        for (j = 1; j < i; j++) s += a[i - 1][j] * r[i - j];

        faac_real k = -s / e;
        if (FAAC_FABS(k) >= 0.99) break;

        kArray[i] = k;
        a[i][i] = k;
        for (j = 1; j < i; j++) a[i][j] = a[i - 1][j] + k * a[i - 1][i - j];
        e *= (1.0 - k * k);
        if (e < 1e-9) { i++; break; }
    }

    *gain = r[0] / (e > 1e-10 ? e : 1e-10);
    return i - 1;
}

static void StepUp(int fOrder, faac_real* kArray, faac_real* aArray)
{
    faac_real a[TNS_MAX_ORDER + 1][TNS_MAX_ORDER + 1];
    int i, j;
    memset(a, 0, sizeof(a));
    memset(aArray, 0, (fOrder + 1) * sizeof(faac_real));

    for (i = 1; i <= fOrder; i++) {
        a[i][i] = kArray[i];
        for (j = 1; j < i; j++) a[i][j] = a[i - 1][j] + kArray[i] * a[i - 1][i - j];
    }
    aArray[0] = 1.0;
    for (i = 1; i <= fOrder; i++) aArray[i] = a[fOrder][i];
}
