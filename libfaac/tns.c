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

/* Spec-compliant TNS coefficient maps from ISO/IEC 14496-3, Table 4.83 */
static const float tns_coef_0_3[8] = {
    0.0000000000f, 0.3420201433f, 0.6427876097f, 0.8660254038f,
    -0.9848077530f, -0.8660254038f, -0.6427876097f, -0.3420201433f
};

static const float tns_coef_0_4[16] = {
    0.0000000000f, 0.1837495178f, 0.3612416662f, 0.5264321629f,
    0.6736956436f, 0.7980172273f, 0.8951632914f, 0.9618256432f,
    -0.9957341763f, -0.9618256432f, -0.8951632914f, -0.7980172273f,
    -0.6736956436f, -0.5264321629f, -0.3612416662f, -0.1837495178f
};

static void Autocorrelation(int maxOrder, int dataSize, const faac_real * data, faac_real * rArray);
static faac_real LevinsonDurbin(int maxOrder, int dataSize, const faac_real * data, faac_real * kArray);
static void StepUp(int fOrder, faac_real* kArray, faac_real* aArray);
static void QuantizeReflectionCoeffs(int fOrder, int coeffRes, faac_real* kArray, int* indexArray);
static void TnsFilter(int length, faac_real * spec, const TnsFilterData * filter, faac_real * temp);

/* TNS max bands based on ISO/IEC 14496-3 Table 4.84 and 4.85 */
static const int tnsMaxBandsLongLow[13] = {
    31, 31, 34, 40, 42, 51, 46, 46, 42, 42, 42, 40, 40
};
static const int tnsMaxBandsShortLow[13] = {
    9, 9, 10, 14, 14, 14, 15, 15, 15, 15, 15, 15, 15
};

void TnsInit(faacEncStruct* hEncoder)
{
    int ch;
    int idx = hEncoder->sampleRateIdx;
    if (idx > 12) idx = 12;

    for (ch = 0; ch < hEncoder->numChannels; ch++) {
        TnsInfo *tns = &hEncoder->coderInfo[ch].tnsInfo;
        tns->tnsMaxBandsLong = tnsMaxBandsLongLow[idx];
        tns->tnsMaxBandsShort = tnsMaxBandsShortLow[idx];
        tns->tnsMinBandNumberLong = 12;
        tns->tnsMinBandNumberShort = 2;
        tns->tnsMaxOrderLong = 12;
        tns->tnsMaxOrderShort = 7;
    }
}

void TnsEncode(TnsInfo* tnsInfo, int numberOfBands, int maxSfb, enum WINDOW_TYPE blockType, int* sfbOffsetTable, faac_real* spec, faac_real* temp)
{
    int numberOfWindows, windowSize, startBand, order, maxBands;
    int w;
    faac_real gain;

    if (blockType == ONLY_SHORT_WINDOW) {
        numberOfWindows = 8;
        windowSize = 128;
        startBand = tnsInfo->tnsMinBandNumberShort;
        maxBands = tnsInfo->tnsMaxBandsShort;
        order = tnsInfo->tnsMaxOrderShort;
    } else {
        numberOfWindows = 1;
        windowSize = 1024;
        startBand = tnsInfo->tnsMinBandNumberLong;
        maxBands = tnsInfo->tnsMaxBandsLong;
        order = tnsInfo->tnsMaxOrderLong;
    }

    /* Clamp maxBands to available bands to avoid out-of-bounds */
    maxBands = min(maxBands, numberOfBands);
    startBand = min(startBand, maxBands);

    tnsInfo->tnsDataPresent = 0;

    for (w = 0; w < numberOfWindows; w++) {
        TnsWindowData* windowData = &tnsInfo->windowData[w];
        TnsFilterData* tnsFilter = windowData->tnsFilter;

        windowData->numFilters = 0;
        /* startIndex is relative to the start of the current window */
        int windowOffset = w * windowSize;
        int startIndex = windowOffset + sfbOffsetTable[startBand];
        int lengthInLines = sfbOffsetTable[maxBands] - sfbOffsetTable[startBand];

        if (lengthInLines <= order) continue;

        gain = LevinsonDurbin(order, lengthInLines, &spec[startIndex], tnsFilter->kCoeffs);

        /* TNS gain threshold. AAC spec suggests TNS should be used if it provides
           significant prediction gain. 1.6 is approx 2dB. */
        if (gain > 1.6) {
            /* Energy slant heuristic for direction */
            faac_real e_low = 0, e_high = 0;
            int mid = lengthInLines / 2;
            int i;
            for (i = 0; i < mid; i++) e_low += spec[startIndex + i] * spec[startIndex + i];
            for (i = mid; i < lengthInLines; i++) e_high += spec[startIndex + i] * spec[startIndex + i];

            tnsFilter->direction = (e_high > e_low) ? 1 : 0;

            QuantizeReflectionCoeffs(order, 4, tnsFilter->kCoeffs, tnsFilter->index);
            windowData->numFilters = 1;
            windowData->coefResolution = 4;
            tnsInfo->tnsDataPresent = 1;
            tnsFilter->order = order;
            tnsFilter->length = maxBands - startBand;
            tnsFilter->coefCompress = 0;

            StepUp(order, tnsFilter->kCoeffs, tnsFilter->aCoeffs);
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
       The encoder must use FIR to be the inverse of the decoder's IIR filter. */
    memcpy(temp, spec, length * sizeof(faac_real));

    if (filter->direction == 0) { /* Forward */
        for (i = 0; i < length; i++) {
            faac_real acc = 0;
            for (j = 1; j <= order && j <= i; j++) {
                acc += a[j] * temp[i - j];
            }
            /* Prediction error: e[n] = x[n] + sum(a[j] * x[n-j]) */
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
    const float* map = (coeffRes == 4) ? tns_coef_0_4 : tns_coef_0_3;
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

static faac_real LevinsonDurbin(int fOrder, int dataSize, const faac_real * data, faac_real * kArray)
{
    faac_real r[TNS_MAX_ORDER + 1] = {0};
    faac_real a[TNS_MAX_ORDER + 1][TNS_MAX_ORDER + 1] = {{0}};
    faac_real e;
    int i, j;

    Autocorrelation(fOrder, dataSize, data, r);
    if (r[0] < 1e-9) return 0;

    e = r[0];
    for (i = 1; i <= fOrder; i++) {
        faac_real s = r[i];
        for (j = 1; j < i; j++) s += a[i - 1][j] * r[i - j];
        kArray[i] = -s / e;
        a[i][i] = kArray[i];
        for (j = 1; j < i; j++) a[i][j] = a[i - 1][j] + kArray[i] * a[i - 1][i - j];
        e *= (1.0 - kArray[i] * kArray[i]);
        if (e < 1e-9) break;
    }
    return r[0] / e;
}

static void StepUp(int fOrder, faac_real* kArray, faac_real* aArray)
{
    faac_real a[TNS_MAX_ORDER + 1][TNS_MAX_ORDER + 1];
    int i, j;
    /* Standard Step-up: converts reflection coefficients to prediction coefficients.
       ISO/IEC 14496-3, 4.6.8.4.3 */
    for (i = 1; i <= fOrder; i++) {
        a[i][i] = kArray[i];
        for (j = 1; j < i; j++) a[i][j] = a[i - 1][j] + kArray[i] * a[i - 1][i - j];
    }
    aArray[0] = 1.0;
    for (i = 1; i <= fOrder; i++) aArray[i] = a[fOrder][i];
}
