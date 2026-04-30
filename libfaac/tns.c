/**********************************************************************
This software module was originally developed by Texas Instruments
and edited by         in the course of
development of the MPEG-2 NBC/MPEG-4 Audio standard
ISO/IEC 13818-7, 14496-1,2 and 3.
**********************************************************************/

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "frame.h"
#include "coder.h"
#include "bitstream.h"
#include "tns.h"
#include "util.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static unsigned short tnsMinBandNumberLong[12] = { 12, 15, 15, 20, 21, 23, 22, 23, 28, 30, 31, 31 };
static unsigned short tnsMinBandNumberShort[12] = { 2, 2, 2, 3, 3, 4, 6, 6, 8, 9, 10, 11 };
static unsigned short tnsMaxBandsLongMainLow[12] = { 40, 31, 34, 40, 42, 51, 46, 46, 42, 42, 42, 39 };
static unsigned short tnsMaxBandsShortMainLow[12] = { 14, 9, 10, 14, 14, 14, 14, 14, 14, 14, 14, 14 };

static faac_real LevinsonDurbin(int fOrder, const faac_real* rArray, faac_real* kArray);
static void StepUp(int fOrder, faac_real* kArray, faac_real* aArray);
static void QuantizeReflectionCoeffs(int fOrder,int coeffRes,faac_real* rArray,int* indexArray);
static int TruncateCoeffs(int fOrder,faac_real threshold,faac_real* kArray);
static void TnsInvFilter(int length,faac_real* spec,TnsFilterData* filter, faac_real *temp);

static void CalcGaussWindow(faac_real *win, int winSize, int samplingRate, int blockType, double timeResolution) {
    int i;
    double gaussExp = M_PI * (double)samplingRate * 0.001 * timeResolution / (blockType != ONLY_SHORT_WINDOW ? 1024.0 : 128.0);
    gaussExp = -0.5 * gaussExp * gaussExp;
    for (i = 0; i < winSize; i++) win[i] = (faac_real)exp(gaussExp * (i + 0.5) * (i + 0.5));
}

static void CalcWeightedSpectrum(const faac_real spectrum[], faac_real weightedSpectrum[], int numberOfBands, const int* sfbOffset, int lpcStartBand, int lpcStopBand) {
    int i, sfb;
    faac_real tnsSfbMean[NSFB_LONG], tmp, totalE = 1e-10;
    for (sfb = lpcStartBand; sfb < lpcStopBand; sfb++) {
        faac_real energy = 1e-10;
        int bandStart = sfbOffset[sfb], bandEnd = sfbOffset[sfb+1];
        for (i = bandStart; i < bandEnd; i++) energy += spectrum[i] * spectrum[i];
        totalE += energy;
        tnsSfbMean[sfb] = (faac_real)(1.0 / sqrt(energy / (bandEnd - bandStart)));
    }
    faac_real floor = 1.0 / sqrt(totalE * 1e-3 / (sfbOffset[lpcStopBand] - sfbOffset[lpcStartBand]) + 1e-10);
    for (sfb = lpcStartBand; sfb < lpcStopBand; sfb++) if (tnsSfbMean[sfb] > floor) tnsSfbMean[sfb] = floor;
    sfb = lpcStartBand; tmp = (lpcStartBand < NSFB_LONG) ? tnsSfbMean[sfb] : 0.0;
    for (i = sfbOffset[lpcStartBand]; i < sfbOffset[lpcStopBand]; i++) {
        if (sfb + 1 < lpcStopBand && sfbOffset[sfb+1] == i) { sfb++; tmp = tnsSfbMean[sfb]; }
        weightedSpectrum[i] = tmp;
    }
    for (i = sfbOffset[lpcStopBand] - 2; i >= sfbOffset[lpcStartBand]; i--) weightedSpectrum[i] = (weightedSpectrum[i] + weightedSpectrum[i+1]) * 0.5;
    for (i = sfbOffset[lpcStartBand] + 1; i < sfbOffset[lpcStopBand]; i++) weightedSpectrum[i] = (weightedSpectrum[i] + weightedSpectrum[i-1]) * 0.5;
    for (i = sfbOffset[lpcStartBand]; i < sfbOffset[lpcStopBand]; i++) weightedSpectrum[i] *= spectrum[i];
}

void TnsInit(faacEncStruct* hEncoder) {
    unsigned int channel; int fsIndex = hEncoder->sampleRateIdx;
    for (channel = 0; channel < hEncoder->numChannels; channel++) {
        TnsInfo *tnsInfo = &hEncoder->coderInfo[channel].tnsInfo;
        tnsInfo->tnsMaxBandsLong = tnsMaxBandsLongMainLow[fsIndex];
        tnsInfo->tnsMaxBandsShort = tnsMaxBandsShortMainLow[fsIndex];
        tnsInfo->tnsMaxOrderLong = 12; tnsInfo->tnsMaxOrderShort = 5;
        tnsInfo->tnsMinBandNumberLong = tnsMinBandNumberLong[fsIndex];
        tnsInfo->tnsMinBandNumberShort = tnsMinBandNumberShort[fsIndex];
        CalcGaussWindow(tnsInfo->acfWindowLong, TNS_MAX_ORDER + 1, hEncoder->sampleRate, ONLY_LONG_WINDOW, 0.75);
        CalcGaussWindow(tnsInfo->acfWindowShort, TNS_MAX_ORDER + 1, hEncoder->sampleRate, ONLY_SHORT_WINDOW, 0.75);
    }
}

void TnsEncode(TnsInfo* tnsInfo, int numberOfBands, int maxSfb, enum WINDOW_TYPE blockType, int* sfbOffsetTable, faac_real* spec, faac_real* temp, int bitRatePerChannel) {
    int numberOfWindows,windowSize,startBand,stopBand,order,w,i,startIndex,length;
    faac_real gain, thresh, rArray[TNS_MAX_ORDER + 1], *weightedSpec = temp, *acfWin;
    if (blockType == ONLY_SHORT_WINDOW) {
        numberOfWindows = MAX_SHORT_WINDOWS; windowSize = BLOCK_LEN_SHORT; startBand = tnsInfo->tnsMinBandNumberShort;
        stopBand = min(numberOfBands, tnsInfo->tnsMaxBandsShort); order = tnsInfo->tnsMaxOrderShort; acfWin = tnsInfo->acfWindowShort;
    } else {
        numberOfWindows = 1; windowSize = BLOCK_LEN_LONG; startBand = tnsInfo->tnsMinBandNumberLong;
        stopBand = min(numberOfBands, tnsInfo->tnsMaxBandsLong); order = tnsInfo->tnsMaxOrderLong; acfWin = tnsInfo->acfWindowLong;
    }
    startBand = min(max(startBand, 0), maxSfb); stopBand = min(max(stopBand, 0), maxSfb);
    tnsInfo->tnsDataPresent = 0;
    for (w=0;w<numberOfWindows;w++) {
        TnsWindowData* windowData = &tnsInfo->windowData[w];
        TnsFilterData* tnsFilter = windowData->tnsFilter;
        windowData->numFilters=0; windowData->coefResolution = DEF_TNS_COEFF_RES;
        startIndex = w * windowSize + sfbOffsetTable[startBand]; length = sfbOffsetTable[stopBand] - sfbOffsetTable[startBand];
        if (length <= 0) continue;
        faac_real totalE = 0.0;
        for (i = 0; i < length; i++) totalE += spec[startIndex + i] * spec[startIndex + i];
        if (UNLIKELY(totalE < 100.0)) continue;
        CalcWeightedSpectrum(&spec[w * windowSize], weightedSpec, numberOfBands, sfbOffsetTable, startBand, stopBand);
        faac_real *ptr = &weightedSpec[sfbOffsetTable[startBand]];
        rArray[0] = 0.0; for (i=0; i<length; i++) rArray[0] += ptr[i] * ptr[i];
        for (i=1; i<=2; i++) {
            faac_real accu = 0.0; const faac_real *p1 = ptr, *p2 = p1 + i;
            int count = length - i, j;
            for (j=0; j < count - 3; j += 4) accu += p1[j] * p2[j] + p1[j+1] * p2[j+1] + p1[j+2] * p2[j+2] + p1[j+3] * p2[j+3];
            for (; j < count; j++) accu += p1[j] * p2[j];
            rArray[i] = accu * acfWin[i];
        }
        gain = LevinsonDurbin(2, rArray, tnsFilter->kCoeffs);
        if (UNLIKELY(gain < 1.05)) continue;
        for (i=3; i<=order; i++) {
            faac_real accu = 0.0; const faac_real *p1 = ptr, *p2 = p1 + i;
            int count = length - i, j;
            for (j=0; j < count - 3; j += 4) accu += p1[j] * p2[j] + p1[j+1] * p2[j+1] + p1[j+2] * p2[j+2] + p1[j+3] * p2[j+3];
            for (; j < count; j++) accu += p1[j] * p2[j];
            rArray[i] = accu * acfWin[i];
        }
        gain = LevinsonDurbin(order, rArray, tnsFilter->kCoeffs);
        if (bitRatePerChannel < 24000) thresh = 2.5; else if (bitRatePerChannel < 48000) thresh = 2.0; else thresh = 1.41;
        if (bitRatePerChannel < 48000) order = min(order, 7);
        if (blockType == ONLY_SHORT_WINDOW && bitRatePerChannel < 64000) continue;
        if (gain > thresh) {
            windowData->numFilters++; tnsInfo->tnsDataPresent=1; tnsFilter->direction = 0; tnsFilter->coefCompress = 0;
            tnsFilter->length = stopBand - startBand;
            QuantizeReflectionCoeffs(order,DEF_TNS_COEFF_RES,tnsFilter->kCoeffs,tnsFilter->index);
            int truncatedOrder = TruncateCoeffs(order,DEF_TNS_COEFF_THRESH,tnsFilter->kCoeffs);
            if (truncatedOrder < 3) { windowData->numFilters = 0; continue; }
            tnsFilter->order = truncatedOrder; StepUp(truncatedOrder,tnsFilter->kCoeffs,tnsFilter->aCoeffs);
            TnsInvFilter(length,&spec[startIndex],tnsFilter,weightedSpec + 1024);
        }
    }
}

void TnsEncodeFilterOnly(TnsInfo* tnsInfo, int numberOfBands, int maxSfb, enum WINDOW_TYPE blockType, int* sfbOffsetTable, faac_real* spec, faac_real* temp) {
    int numberOfWindows,windowSize,startBand,stopBand,w,startIndex,length;
    if (blockType == ONLY_SHORT_WINDOW) {
        numberOfWindows = MAX_SHORT_WINDOWS; windowSize = BLOCK_LEN_SHORT; startBand = tnsInfo->tnsMinBandNumberShort;
        stopBand = min(numberOfBands, tnsInfo->tnsMaxBandsShort);
    } else {
        numberOfWindows = 1; windowSize = BLOCK_LEN_LONG; startBand = tnsInfo->tnsMinBandNumberLong;
        stopBand = min(numberOfBands, tnsInfo->tnsMaxBandsLong);
    }
    startBand = min(max(startBand, 0), maxSfb); stopBand = min(max(stopBand, 0), maxSfb);
    for(w=0;w<numberOfWindows;w++) {
        TnsWindowData* windowData = &tnsInfo->windowData[w];
        TnsFilterData* tnsFilter = windowData->tnsFilter;
        startIndex = w * windowSize + sfbOffsetTable[startBand]; length = sfbOffsetTable[stopBand] - sfbOffsetTable[startBand];
        if (tnsInfo->tnsDataPresent && windowData->numFilters) TnsInvFilter(length,&spec[startIndex],tnsFilter,temp);
    }
}

static void TnsInvFilter(int length, faac_real* spec, TnsFilterData* filter, faac_real *temp) {
    int i,j,order=filter->order; faac_real* a=filter->aCoeffs;
    memcpy(temp, spec, length * sizeof(faac_real));
    if (filter->direction) {
        for (i=0; i < length; i++) {
            faac_real val = temp[i];
            for (j=1; j <= order && i + j < length; j++) val += temp[i+j] * a[j];
            spec[i] = val;
        }
    } else {
        for (i=0; i < length; i++) {
            faac_real val = temp[i];
            for (j=1; j <= order && i - j >= 0; j++) val += temp[i-j] * a[j];
            spec[i] = val;
        }
    }
}

static int TruncateCoeffs(int fOrder,faac_real threshold,faac_real* kArray) {
    for (int i = fOrder; i > 0; i--) { if (FAAC_FABS(kArray[i]) > threshold) return i; kArray[i] = 0.0; }
    return 0;
}

static void QuantizeReflectionCoeffs(int fOrder, int coeffRes, faac_real* kArray, int* indexArray) {
    faac_real iqfac = ((1<<(coeffRes-1))-0.5)/(M_PI/2), iqfac_m = ((1<<(coeffRes-1))+0.5)/(M_PI/2);
    for (int i=1;i<=fOrder;i++) {
        indexArray[i] = (kArray[i]>=0)?(int)(0.5+(FAAC_ASIN(kArray[i])*iqfac)):(int)(-0.5+(FAAC_ASIN(kArray[i])*iqfac_m));
        kArray[i] = FAAC_SIN((faac_real)indexArray[i]/((indexArray[i]>=0)?iqfac:iqfac_m));
    }
}

static faac_real LevinsonDurbin(int fOrder, const faac_real* rArray, faac_real* kArray) {
    int order,i; faac_real signal=rArray[0], error, kTemp, num;
    faac_real aArray1[TNS_MAX_ORDER+1], aArray2[TNS_MAX_ORDER+1], *aPtr = aArray1, *aLastPtr = aArray2, *aTemp;
    if (signal <= 1e-9) { for (order=1;order<=fOrder;order++) kArray[order]=0.0; return 0.0; }
    error = signal; aPtr[0] = aLastPtr[0] = 1.0;
    for (order=1;order<=fOrder;order++) {
        num = 0.0; for (i=0; i<order; i++) num -= aLastPtr[i] * rArray[order-i];
        kTemp = num / error; if (FAAC_FABS(kTemp) >= 1.0) break;
        kArray[order] = kTemp; aPtr[order] = kTemp;
        for (i=1; i < order; i++) aPtr[i] = aLastPtr[i] + kTemp * aLastPtr[order-i];
        error *= (1.0 - kTemp * kTemp); if (error <= 1e-9) break;
        aTemp = aLastPtr; aLastPtr = aPtr; aPtr = aTemp;
    }
    return (error > 1e-9) ? signal / error : 100.0;
}

static void StepUp(int fOrder,faac_real* kArray,faac_real* aArray) {
    faac_real aTemp[TNS_MAX_ORDER+2];
    aArray[0]=aTemp[0]=1.0;
    for (int order=1;order<=fOrder;order++) {
        for (int i=1;i<order;i++) aTemp[i] = aArray[i] + kArray[order]*aArray[order-i];
        aTemp[order] = kArray[order];
        for (int i=1;i<=order;i++) aArray[i]=aTemp[i];
    }
}
