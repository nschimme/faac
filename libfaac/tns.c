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

static void Autocorrelation(int maxOrder, int dataSize, faac_real* data, faac_real* rArray);
static faac_real LevinsonDurbin(int maxOrder, int dataSize, faac_real* data, faac_real* kArray);
static void StepUp(int fOrder, faac_real* kArray, faac_real* aArray);
static void QuantizeReflectionCoeffs(int fOrder,int coeffRes,faac_real* rArray,int* indexArray);
static int TruncateCoeffs(int fOrder,faac_real threshold,faac_real* kArray);
static void TnsInvFilter(int length,faac_real* spec,TnsFilterData* filter, faac_real *temp);

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
        gain_thresh = 4.0; /* Higher threshold for long blocks to avoid regressions */
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

        for (i = 0; i < length; i++) {
            faac_real val2 = spec[startIndex + i] * spec[startIndex + i] + 1e-15;
            energy += val2;
            logSum += log(val2);
        }

        if (energy < 1e-9) continue;
        sfm = exp(logSum / length) / (energy / length);

        faac_real predGain = LevinsonDurbin(order,length,&spec[startIndex],k);
        faac_real side_info_cost = (order * 4 + 10) * lambda;

        if (predGain > gain_thresh && sfm > sfm_thresh && (predGain - 1.0) * energy * 0.1 > side_info_cost) {
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
            TnsInvFilter(length,&spec[startIndex],tnsFilter,temp);
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
            TnsInvFilter(length,&spec[startIndex],tnsFilter,temp);
        }
    }
}

static void TnsInvFilter(int length,faac_real* spec,TnsFilterData* filter, faac_real *temp)
{
    int i,j,k=0;
    int order=filter->order;
    faac_real* a=filter->aCoeffs;

    if (filter->direction) {
        temp[length-1]=spec[length-1];
        for (i=length-2;i>(length-1-order);i--) {
            temp[i]=spec[i];
            k++;
            for (j=1;j<=k;j++) {
                spec[i]+=temp[i+j]*a[j];
            }
        }
        for (i=length-1-order;i>=0;i--) {
            temp[i]=spec[i];
            for (j=1;j<=order;j++) {
                spec[i]+=temp[i+j]*a[j];
            }
        }
    } else {
        temp[0]=spec[0];
        for (i=1;i<order;i++) {
            temp[i]=spec[i];
            for (j=1;j<=i;j++) {
                spec[i]+=temp[i-j]*a[j];
            }
        }
        for (i=order;i<length;i++) {
            temp[i]=spec[i];
            for (j=1;j<=order;j++) {
                spec[i]+=temp[i-j]*a[j];
            }
        }
    }
}

static int TruncateCoeffs(int fOrder,faac_real threshold,faac_real* kArray)
{
    int i;
    for (i = fOrder; i >= 0; i--) {
        kArray[i] = (FAAC_FABS(kArray[i])>threshold) ? kArray[i] : 0.0;
        if (kArray[i]!=0.0) return i;
    }
    return 0;
}

static void QuantizeReflectionCoeffs(int fOrder, int coeffRes, faac_real* kArray, int* indexArray)
{
    faac_real iqfac,iqfac_m;
    int i;
    iqfac = ((1<<(coeffRes-1))-0.5)/(M_PI/2);
    iqfac_m = ((1<<(coeffRes-1))+0.5)/(M_PI/2);
    for (i=1;i<=fOrder;i++) {
        indexArray[i] = (kArray[i]>=0)?(int)(0.5+(FAAC_ASIN(kArray[i])*iqfac)):(int)(-0.5+(FAAC_ASIN(kArray[i])*iqfac_m));
        if (indexArray[i] > 7) indexArray[i] = 7;
        if (indexArray[i] < -8) indexArray[i] = -8;
        kArray[i] = FAAC_SIN((faac_real)indexArray[i]/((indexArray[i]>=0)?iqfac:iqfac_m));
        if (kArray[i] > 0.8) kArray[i] = 0.8;
        if (kArray[i] < -0.8) kArray[i] = -0.8;
    }
}

static void Autocorrelation(int maxOrder, int dataSize, faac_real* data, faac_real* rArray)
{
    int order,index;
    for (order=0;order<=maxOrder;order++) {
        rArray[order]=0.0;
        for (index=0;index<dataSize;index++) {
            rArray[order]+=data[index]*data[index+order];
        }
        dataSize--;
    }
}

static faac_real LevinsonDurbin(int fOrder, int dataSize, faac_real* data, faac_real* kArray)
{
    int order,i;
    faac_real signal;
    faac_real error, kTemp;
    faac_real rArray[TNS_MAX_ORDER+1] = {0};

    Autocorrelation(fOrder,dataSize,data,rArray);
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
        faac_real* aPtr = aArray1;
        faac_real* aLastPtr = aArray2;
        faac_real* aTemp;
        kArray[0]=1.0;
        aPtr[0]=1.0;
        aLastPtr[0]=1.0;
        error=rArray[0];
        for (order=1;order<=fOrder;order++) {
            kTemp = aLastPtr[0]*rArray[order];
            for (i=1;i<order;i++) {
                kTemp += aLastPtr[i]*rArray[order-i];
            }
            kTemp = -kTemp/error;
            kArray[order]=kTemp;
            aPtr[order]=kTemp;
            for (i=1;i<order;i++) {
                aPtr[i] = aLastPtr[i] + kTemp*aLastPtr[order-i];
            }
            error = error * (1 - kTemp*kTemp);
            aTemp=aLastPtr;
            aLastPtr=aPtr;
            aPtr=aTemp;
        }
        return signal/error;
    }
}

static void StepUp(int fOrder,faac_real* kArray,faac_real* aArray)
{
    faac_real a_temp[TNS_MAX_ORDER+2];
    int i,order;
    aArray[0]=1.0;
    a_temp[0]=1.0;
    for (order=1;order<=fOrder;order++) {
        aArray[order]=0.0;
        for (i=1;i<=order;i++) {
            a_temp[i] = aArray[i] + kArray[order]*aArray[order-i];
        }
        for (i=1;i<=order;i++) {
            aArray[i]=a_temp[i];
        }
    }
}
