/**********************************************************************
Temporal Noise Shaping (TNS) - Master Quality Implementation
**********************************************************************/
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "frame.h"
#include "coder.h"
#include "bitstream.h"
#include "tns.h"
#include "util.h"

static unsigned short tnsMinBandNumberLong[12] = { 11, 12, 15, 16, 17, 20, 25, 26, 24, 28, 30, 31 };
static unsigned short tnsMinBandNumberShort[12] = { 2, 2, 2, 3, 3, 4, 6, 6, 8, 10, 10, 12 };
static unsigned short tnsMaxBandsLongLow[12] = { 31, 31, 34, 40, 42, 51, 46, 46, 42, 42, 42, 39 };
static unsigned short tnsMaxBandsShortLow[12] = { 9, 9, 10, 14, 14, 14, 14, 14, 14, 14, 14, 14 };

#define TNS_SPECTRAL_FRAC   0.65
#define TNS_FIXED_OVERHEAD  14
#define TNS_CALIBRATION     0.90
#define TNS_THRESH_FLOOR    1.05
#define TNS_THRESH_CAP      2.00

static void Autocorrelation(int maxOrder, int dataSize, const faac_real * restrict data, faac_real * restrict rArray);
static faac_real LevinsonDurbin(int maxOrder, int dataSize, const faac_real * restrict data, faac_real * restrict kArray);
static void StepUp(int fOrder, faac_real* kArray, faac_real* aArray);
static void QuantizeReflectionCoeffs(int fOrder,int coeffRes,faac_real* rArray,int* indexArray);
static int TruncateCoeffs(int fOrder,faac_real threshold,faac_real* kArray);
static void TnsInvFilter(int length, faac_real * restrict spec, const TnsFilterData * restrict filter, faac_real * restrict temp);

void TnsInit(faacEncStruct* hEncoder) {
    unsigned int channel;
    int fsIndex = hEncoder->sampleRateIdx;
    unsigned long bitratePerCh = hEncoder->config.bitRate;
    unsigned long quality = hEncoder->config.quantqual;
    unsigned long effectiveBitratePerCh = bitratePerCh > 0 ? bitratePerCh : (quality * 1280) / hEncoder->numChannels;

    /* TNS Gate: Active up to 96kbps/ch to capture quality gains at music_std. */
    int tnsGated = (effectiveBitratePerCh >= 96000);
    hEncoder->config.useTns = (hEncoder->config.useTns != 0) && !tnsGated;

    for (channel = 0; channel < hEncoder->numChannels; channel++) {
        TnsInfo *tnsInfo = &hEncoder->coderInfo[channel].tnsInfo;
        tnsInfo->tnsMaxBandsLong = tnsMaxBandsLongLow[fsIndex];
        tnsInfo->tnsMaxBandsShort = tnsMaxBandsShortLow[fsIndex];
        tnsInfo->tnsMaxOrderShort = 7;
        tnsInfo->tnsMinBandNumberLong = tnsMinBandNumberLong[fsIndex];
        tnsInfo->tnsMinBandNumberShort = tnsMinBandNumberShort[fsIndex];
        tnsInfo->tnsDisabled = !hEncoder->config.useTns;
        if (tnsInfo->tnsDisabled) continue;

        tnsInfo->tnsMaxOrderLong = 12;
        int frame_bits = (int)(effectiveBitratePerCh * FRAME_LEN / hEncoder->sampleRate);
        int spectral_bits = (int)(frame_bits * TNS_SPECTRAL_FRAC);
        {
            int tns_overhead = tnsInfo->tnsMaxOrderLong * DEF_TNS_COEFF_RES + TNS_FIXED_OVERHEAD;
            int denom = spectral_bits - tns_overhead;
            faac_real thresh = (denom <= 0) ? (faac_real)TNS_THRESH_CAP : ((faac_real)spectral_bits / (faac_real)denom) * (faac_real)TNS_CALIBRATION;
            if (thresh < (faac_real)TNS_THRESH_FLOOR) thresh = (faac_real)TNS_THRESH_FLOOR;
            if (thresh > (faac_real)TNS_THRESH_CAP) thresh = (faac_real)TNS_THRESH_CAP;
            tnsInfo->gainThreshLong = thresh;
        }
        {
            int tns_overhead = (tnsInfo->tnsMaxOrderShort * DEF_TNS_COEFF_RES + TNS_FIXED_OVERHEAD) * MAX_SHORT_WINDOWS;
            int denom = spectral_bits - tns_overhead;
            faac_real thresh = (denom <= 0) ? (faac_real)TNS_THRESH_CAP : ((faac_real)spectral_bits / (faac_real)denom) * (faac_real)TNS_CALIBRATION;
            if (thresh < (faac_real)TNS_THRESH_FLOOR) thresh = (faac_real)TNS_THRESH_FLOOR;
            if (thresh > (faac_real)TNS_THRESH_CAP) thresh = (faac_real)TNS_THRESH_CAP;
            tnsInfo->gainThreshShort = thresh * 0.85; /* More sensitive for transient blocks. */
        }
    }
}

void TnsEncode(TnsInfo* tnsInfo, int numberOfBands, int maxSfb, enum WINDOW_TYPE blockType, int* sfbOffsetTable, faac_real* spec, faac_real* temp) {
    int numberOfWindows,windowSize;
    if (tnsInfo->tnsDisabled) { tnsInfo->tnsDataPresent = 0; return; }
    int startBand,stopBand,order;
    int lengthInBands;
    int w, i, startIndex, length;
    faac_real gain;
    switch( blockType ) {
    case ONLY_SHORT_WINDOW :
        numberOfWindows = MAX_SHORT_WINDOWS; windowSize = BLOCK_LEN_SHORT;
        startBand = tnsInfo->tnsMinBandNumberShort; stopBand = maxSfb;
        lengthInBands = stopBand - startBand; order = tnsInfo->tnsMaxOrderShort;
        break;
    default:
        numberOfWindows = 1; windowSize = BLOCK_LEN_LONG;
        startBand = tnsInfo->tnsMinBandNumberLong; stopBand = maxSfb;
        lengthInBands = stopBand - startBand; order = tnsInfo->tnsMaxOrderLong;
        startBand = min(startBand,tnsInfo->tnsMaxBandsLong); stopBand = min(stopBand,tnsInfo->tnsMaxBandsLong);
        break;
    }
    startBand = min(max(startBand,0),maxSfb); stopBand = min(max(stopBand,0),maxSfb);
    tnsInfo->tnsDataPresent = 0;
    for (w=0;w<numberOfWindows;w++) {
        TnsWindowData* windowData = &tnsInfo->windowData[w];
        TnsFilterData* tnsFilter = windowData->tnsFilter;
        windowData->numFilters=0;
        windowData->coefResolution = DEF_TNS_COEFF_RES;
        startIndex = w * windowSize + sfbOffsetTable[startBand];
        length = sfbOffsetTable[stopBand] - sfbOffsetTable[startBand];
        if (length <= 0) continue;
        gain = LevinsonDurbin(order,length,&spec[startIndex],tnsFilter->kCoeffs);
        faac_real currentThresh = (blockType == ONLY_SHORT_WINDOW) ? tnsInfo->gainThreshShort : tnsInfo->gainThreshLong;
        if (gain > currentThresh) {
            int truncatedOrder;
            QuantizeReflectionCoeffs(order,DEF_TNS_COEFF_RES,tnsFilter->kCoeffs,tnsFilter->index);
            truncatedOrder = TruncateCoeffs(order,DEF_TNS_COEFF_THRESH,tnsFilter->kCoeffs);
            if (truncatedOrder == 0) continue;
            windowData->numFilters++; tnsInfo->tnsDataPresent=1; tnsFilter->direction = 0;
            tnsFilter->coefCompress = 1;
            for (i = 1; i <= truncatedOrder; i++) {
                if (tnsFilter->index[i] < -4 || tnsFilter->index[i] > 3) { tnsFilter->coefCompress = 0; break; }
            }
            tnsFilter->length = lengthInBands; tnsFilter->order = truncatedOrder;
            StepUp(truncatedOrder,tnsFilter->kCoeffs,tnsFilter->aCoeffs);
            TnsInvFilter(length,&spec[startIndex],tnsFilter,temp);
        }
    }
}

void TnsEncodeFilterOnly(TnsInfo* tnsInfo, int numberOfBands, int maxSfb, enum WINDOW_TYPE blockType, int* sfbOffsetTable, faac_real* spec, faac_real* temp) {
    int numberOfWindows,windowSize;
    if (tnsInfo->tnsDisabled) { tnsInfo->tnsDataPresent = 0; return; }
    int startBand,stopBand;
    int w;
    switch( blockType ) {
    case ONLY_SHORT_WINDOW :
        numberOfWindows = MAX_SHORT_WINDOWS; windowSize = BLOCK_LEN_SHORT;
        startBand = tnsInfo->tnsMinBandNumberShort; stopBand = maxSfb;
        break;
    default:
        numberOfWindows = 1; windowSize = BLOCK_LEN_LONG;
        startBand = tnsInfo->tnsMinBandNumberLong; stopBand = maxSfb;
        startBand = min(startBand,tnsInfo->tnsMaxBandsLong); stopBand = min(stopBand,tnsInfo->tnsMaxBandsLong);
        break;
    }
    startBand = min(max(startBand,0),maxSfb); stopBand = min(max(stopBand,0),maxSfb);
    for(w=0;w<numberOfWindows;w++) {
        TnsWindowData* windowData = &tnsInfo->windowData[w];
        if (tnsInfo->tnsDataPresent && windowData->numFilters) {
            int startIndex = w * windowSize + sfbOffsetTable[startBand];
            int length = sfbOffsetTable[stopBand] - sfbOffsetTable[startBand];
            TnsInvFilter(length,&spec[startIndex],windowData->tnsFilter,temp);
        }
    }
}

static void TnsInvFilter(int length, faac_real * restrict spec, const TnsFilterData * restrict filter, faac_real * restrict temp) {
    int i, j; const int order = filter->order; const faac_real * restrict a = filter->aCoeffs;
    if (filter->direction) {
        temp[length-1] = spec[length-1];
        for (i = length-2; i > (length-1-order); i--) {
            faac_real acc = spec[i]; temp[i] = acc;
            for (j = 1; j <= (length-1-i); j++) acc += temp[i+j] * a[j];
            spec[i] = acc;
        }
        for (i = length-1-order; i >= 0; i--) {
            faac_real acc = spec[i]; temp[i] = acc;
            for (j = 1; j <= order; j++) acc += temp[i+j] * a[j];
            spec[i] = acc;
        }
    } else {
        temp[0] = spec[0];
        for (i = 1; i < order; i++) {
            faac_real acc = spec[i]; temp[i] = acc;
            for (j = 1; j <= i; j++) acc += temp[i-j] * a[j];
            spec[i] = acc;
        }
        for (i = order; i < length; i++) {
            faac_real acc = spec[i]; temp[i] = acc;
            for (j = 1; j <= order; j++) acc += temp[i-j] * a[j];
            spec[i] = acc;
        }
    }
}

static int TruncateCoeffs(int fOrder,faac_real threshold,faac_real* kArray) {
    int i;
    for (i = fOrder; i >= 0; i--) {
        kArray[i] = (FAAC_FABS(kArray[i])>threshold) ? kArray[i] : 0.0;
        if (kArray[i]!=0.0) return i;
    }
    return 0;
}

static void QuantizeReflectionCoeffs(int fOrder, int coeffRes, faac_real* kArray, int* indexArray) {
    faac_real iqfac = ((1<<(coeffRes-1))-0.5)/(M_PI/2);
    faac_real iqfac_m = ((1<<(coeffRes-1))+0.5)/(M_PI/2);
    int i;
    for (i=1;i<=fOrder;i++) {
        indexArray[i] = (kArray[i]>=0)?(int)(0.5+(FAAC_ASIN(kArray[i])*iqfac)):(int)(-0.5+(FAAC_ASIN(kArray[i])*iqfac_m));
        kArray[i] = FAAC_SIN((faac_real)indexArray[i]/((indexArray[i]>=0)?iqfac:iqfac_m));
    }
}

static void Autocorrelation(int maxOrder, int dataSize, const faac_real * restrict data, faac_real * restrict rArray) {
    int order, index;
    for (order = 0; order <= maxOrder; order++) rArray[order] = 0.0;
    int limit = dataSize - maxOrder;
    for (index = 0; index < limit; index++) {
        const faac_real d = data[index]; const faac_real * restrict dp = &data[index + 1];
        rArray[0] += d * d;
        for (order = 1; order <= maxOrder; order++) rArray[order] += d * dp[order - 1];
    }
    for (; index < dataSize; index++) {
        const faac_real d = data[index]; int n = dataSize - 1 - index;
        rArray[0] += d * d;
        for (order = 1; order <= n; order++) rArray[order] += d * data[index + order];
    }
}

static faac_real LevinsonDurbin(int fOrder, int dataSize, const faac_real * restrict data, faac_real * restrict kArray) {
    int order,i;
    faac_real signal, error, kTemp, aArray1[TNS_MAX_ORDER+1], aArray2[TNS_MAX_ORDER+1], rArray[TNS_MAX_ORDER+1] = {0};
    faac_real *aPtr = aArray1, *aLastPtr = aArray2, *aTemp;
    Autocorrelation(fOrder,dataSize,data,rArray);
    signal=rArray[0];
    if (!signal) {
        kArray[0]=1.0; for (order=1;order<=fOrder;order++) kArray[order]=0.0;
        return 0;
    } else {
        kArray[0]=1.0; aPtr[0]=1.0; aLastPtr[0]=1.0; error=rArray[0];
        for (order=1;order<=fOrder;order++) {
            kTemp = aLastPtr[0]*rArray[order-0];
            for (i=1;i<order;i++) kTemp += aLastPtr[i]*rArray[order-i];
            if (error <= 0.0 || FAAC_FABS(kTemp) >= error) { error = 0.0; break; }
            kTemp = -kTemp/error;
            kArray[order]=kTemp; aPtr[order]=kTemp;
            for (i=1;i<order;i++) aPtr[i] = aLastPtr[i] + kTemp*aLastPtr[order-i];
            error = error * (1 - kTemp*kTemp);
            if (error <= 0.0) break;
            aTemp=aLastPtr; aLastPtr=aPtr; aPtr=aTemp;
        }
        if (error <= 0.0) return (faac_real)(3.0);
        return signal/error;
    }
}

static void StepUp(int fOrder,faac_real* kArray,faac_real* aArray) {
    faac_real aTemp[TNS_MAX_ORDER+2]; int i,order;
    aArray[0]=1.0; aTemp[0]=1.0;
    for (order=1;order<=fOrder;order++) {
        aArray[order]=0.0;
        for (i=1;i<=order;i++) aTemp[i] = aArray[i] + kArray[order]*aArray[order-i];
        for (i=1;i<=order;i++) aArray[i]=aTemp[i];
    }
}
