/**********************************************************************

This software module was originally developed by Texas Instruments
and edited by Jules in the course of
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
#include <stdlib.h>
#include "frame.h"
#include "coder.h"
#include "bitstream.h"
#include "tns.h"
#include "util.h"

/***********************************************/
/* TNS Profile/Frequency Dependent Parameters  */
/***********************************************/
/* Limit bands to > 2.0 kHz (ISO/IEC 14496-3 Section 4.6.8.1) */
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
static void Autocorrelation(int maxOrder,        /* Maximum autocorr order */
                     int dataSize,        /* Size of the data array */
                     faac_real* data,        /* Data array */
                     faac_real* rArray);     /* Autocorrelation array */

static faac_real LevinsonDurbin(int maxOrder,        /* Maximum filter order */
                      int dataSize,        /* Size of the data array */
                      faac_real* data,        /* Data array */
                      faac_real* kArray);     /* Reflection coeff array */

static void StepUp(int fOrder, faac_real* kArray, faac_real* aArray);

static void QuantizeReflectionCoeffs(int fOrder, int coeffRes, faac_real* rArray, int* indexArray);
static int TruncateCoeffs(int fOrder, faac_real threshold, faac_real* kArray);
static void TnsInvFilter(int length, faac_real* spec, TnsFilterData* filter, faac_real *temp);


/*****************************************************/
/* TnsInit:                                          */
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
void TnsEncode(TnsInfo* tnsInfo,       /* TNS info */
               int numberOfBands,       /* Number of bands per window */
               int maxSfb,              /* max_sfb */
               enum WINDOW_TYPE blockType,   /* block type */
               int* sfbOffsetTable,     /* Scalefactor band offset table */
               faac_real* spec,            /* Spectral data array */
               faac_real* temp)
{
    int numberOfWindows,windowSize;
    int startBand,stopBand,order;    /* Bands over which to apply TNS */
    int w;
    int startIndex,length;
    faac_real gain;

    switch( blockType ) {
    case ONLY_SHORT_WINDOW :
        numberOfWindows = MAX_SHORT_WINDOWS;
        windowSize = BLOCK_LEN_SHORT;
        startBand = tnsInfo->tnsMinBandNumberShort;
        stopBand = numberOfBands;
        /* DEVIATION: Cap order at 4 for short windows to save bits. */
        order = 4;
        startBand = min(startBand, tnsInfo->tnsMaxBandsShort);
        stopBand = min(stopBand, tnsInfo->tnsMaxBandsShort);
        break;

    default:
        numberOfWindows = 1;
        windowSize = BLOCK_LEN_LONG;
        startBand = tnsInfo->tnsMinBandNumberLong;
        stopBand = numberOfBands;
        /* DEVIATION: Cap order at 12 for long windows to preserve bits at lower bitrates. */
        order = 12;
        startBand = min(startBand, tnsInfo->tnsMaxBandsLong);
        stopBand = min(stopBand, tnsInfo->tnsMaxBandsLong);
        break;
    }

    /* Make sure that start and stop bands < maxSfb */
    /* Make sure that start and stop bands >= 0 */
    startBand = min(startBand,maxSfb);
    stopBand = min(stopBand,maxSfb);
    startBand = max(startBand,0);
    stopBand = max(stopBand,0);

    tnsInfo->tnsDataPresent = 0;     /* default TNS not used */

    /* DEVIATION: Adaptive activation threshold (v20).
       TNS activation threshold is fixed at 2.0 (standard recommendation)
       but increased for short windows (+1.0) to balance side-info bit consumption. */
    faac_real threshold = 2.0;
    if (blockType == ONLY_SHORT_WINDOW) threshold += 1.0;

    /* Perform analysis and filtering for each window */
    for (w=0;w<numberOfWindows;w++) {

        TnsWindowData* windowData = &tnsInfo->windowData[w];
        TnsFilterData* tnsFilter = windowData->tnsFilter;
        faac_real* k = tnsFilter->kCoeffs;    /* reflection coeffs */
        faac_real* a = tnsFilter->aCoeffs;    /* prediction coeffs */

        windowData->numFilters=0;
        windowData->coefResolution = DEF_TNS_COEFF_RES;
        startIndex = w * windowSize + sfbOffsetTable[startBand];
        length = sfbOffsetTable[stopBand] - sfbOffsetTable[startBand];

        if (length <= order) continue;

        gain = LevinsonDurbin(order,length,&spec[startIndex],k);

        if (gain > threshold) {  /* Use TNS */
            int truncatedOrder;
            faac_real k_orig[TNS_MAX_ORDER+1];
            memcpy(k_orig, k, (order + 1) * sizeof(faac_real));

            windowData->numFilters++;
            tnsInfo->tnsDataPresent=1;
            tnsFilter->direction = 0;
            tnsFilter->length = stopBand - startBand;

            QuantizeReflectionCoeffs(order,DEF_TNS_COEFF_RES,k,tnsFilter->index);
            truncatedOrder = TruncateCoeffs(order,DEF_TNS_COEFF_THRESH,k);
            tnsFilter->order = truncatedOrder;

            /* ISO/IEC 14496-3 Section 4.6.8.3: Coefficient Compression.
               Allows for 3-bit re-quantization of small coefficients. */
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

            StepUp(truncatedOrder,k,a);    /* Compute predictor coefficients */
            TnsInvFilter(length,&spec[startIndex],tnsFilter,temp);      /* Filter */
        }
    }
}


/*****************************************************/
/* TnsEncodeFilterOnly:                              */
/* This is a stripped-down version of TnsEncode()    */
/* which performs TNS analysis filtering only        */
/*****************************************************/
void TnsEncodeFilterOnly(TnsInfo* tnsInfo,           /* TNS info */
                         int numberOfBands,          /* Number of bands per window */
                         int maxSfb,                 /* max_sfb */
                         enum WINDOW_TYPE blockType, /* block type */
                         int* sfbOffsetTable,        /* Scalefactor band offset table */
                         faac_real* spec,               /* Spectral data array */
                         faac_real* temp)
{
    int numberOfWindows,windowSize;
    int startBand,stopBand;    /* Bands over which to apply TNS */
    int w;
    int startIndex,length;

    switch( blockType ) {
    case ONLY_SHORT_WINDOW :
        numberOfWindows = MAX_SHORT_WINDOWS;
        windowSize = BLOCK_LEN_SHORT;
        startBand = tnsInfo->tnsMinBandNumberShort;
        stopBand = min(numberOfBands, maxSfb);
        break;

    default:
        numberOfWindows = 1;
        windowSize = BLOCK_LEN_LONG;
        startBand = tnsInfo->tnsMinBandNumberLong;
        stopBand = min(numberOfBands, maxSfb);
        break;
    }

    /* Make sure that start and stop bands >= 0 */
    startBand = max(startBand, 0);
    stopBand = max(stopBand, 0);


    /* Perform filtering for each window */
    for(w=0;w<numberOfWindows;w++)
    {
        TnsWindowData* windowData = &tnsInfo->windowData[w];
        TnsFilterData* tnsFilter = windowData->tnsFilter;

        if (tnsInfo->tnsDataPresent  &&  windowData->numFilters) {  /* Use TNS */
            startIndex = w * windowSize + sfbOffsetTable[startBand];
            length = sfbOffsetTable[stopBand] - sfbOffsetTable[startBand];
            TnsInvFilter(length,&spec[startIndex],tnsFilter,temp);
        }
    }
}


/********************************************************/
/* TnsInvFilter:                                        */
/*   Inverse filter the given spec with specified       */
/*   length using the coefficients specified in filter. */
/*   Note that the order and direction are specified    */
/*   within the TnsFilterData structure.                */
/*   Optimized FIR filter implementation using pointers.*/
/********************************************************/
static void TnsInvFilter(int length,faac_real* spec,TnsFilterData* filter, faac_real *temp)
{
    int i,j;
    int order=filter->order;
    faac_real* a=filter->aCoeffs;

    if (length <= 0) return;
    memcpy(temp, spec, length * sizeof(faac_real));

    /* Determine loop parameters for given direction */
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


/*****************************************************/
/* TruncateCoeffs:                                   */
/*   Truncate the given reflection coeffs by zeroing */
/*   coefficients in the tail with absolute value    */
/*   less than the specified threshold.  Return the  */
/*   truncated filter order.                         */
/*****************************************************/
static int TruncateCoeffs(int fOrder,faac_real threshold,faac_real* kArray)
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
/*   Quantize the given array of reflection coeffs   */
/*   to the specified resolution in bits.            */
/*****************************************************/
static void QuantizeReflectionCoeffs(int fOrder,
                              int coeffRes,
                              faac_real* rArray,
                              int* indexArray)
{
    faac_real iqfac,iqfac_m;
    int i, maxIndex, minIndex;

    iqfac = ((1<<(coeffRes-1))-0.5)/(M_PI/2);
    iqfac_m = ((1<<(coeffRes-1))+0.5)/(M_PI/2);
    maxIndex = (1 << (coeffRes - 1)) - 1;
    minIndex = -(1 << (coeffRes - 1));

    /* Quantize and inverse quantize */
    for (i=1;i<=fOrder;i++) {
        int index = (rArray[i]>=0)?(int)(0.5+(FAAC_ASIN(rArray[i])*iqfac)):(int)(-0.5+(FAAC_ASIN(rArray[i])*iqfac_m));
        if (index > maxIndex) index = maxIndex;
        if (index < minIndex) index = minIndex;
        indexArray[i] = index;
        rArray[i] = FAAC_SIN((faac_real)index/((index>=0)?iqfac:iqfac_m));
    }
}


/*****************************************************/
/* Autocorrelation,                                  */
/*   Compute the autocorrelation function            */
/*   estimate for the given data.                    */
/*   Optimized with pointers.                        */
/*****************************************************/
static void Autocorrelation(int maxOrder,        /* Maximum autocorr order */
                     int dataSize,        /* Size of the data array */
                     faac_real* data,        /* Data array */
                     faac_real* rArray)      /* Autocorrelation array */
{
    int order,index;

    for (order=0;order<=maxOrder;order++) {
        faac_real sum = 0.0;
        int limit = dataSize - order;
        faac_real *p1 = data, *p2 = data + order;
        for (index = 0; index < limit; index++) sum += (*p1++) * (*p2++);
        rArray[order] = sum;
    }
}


/*****************************************************/
/* LevinsonDurbin:                                   */
/*   Compute the reflection coefficients for the     */
/*   given data using LevinsonDurbin recursion.      */
/*   Return the prediction gain.                     */
/*****************************************************/
static faac_real LevinsonDurbin(int fOrder,          /* Filter order */
                      int dataSize,        /* Size of the data array */
                      faac_real* data,        /* Data array */
                      faac_real* kArray)      /* Reflection coeff array */
{
    int order,i;
    faac_real signal;
    faac_real error, kTemp;                /* Prediction error */
    faac_real aArray1[TNS_MAX_ORDER+1];    /* Predictor coeff array */
    faac_real aArray2[TNS_MAX_ORDER+1];    /* Predictor coeff array 2 */
    faac_real rArray[TNS_MAX_ORDER+1] = {0}; /* Autocorrelation coeffs */
    faac_real* aPtr = aArray1;             /* Ptr to aArray1 */
    faac_real* aLastPtr = aArray2;         /* Ptr to aArray2 */
    faac_real* aTemp;

    /* Compute autocorrelation coefficients */
    Autocorrelation(fOrder,dataSize,data,rArray);
    signal=rArray[0];   /* signal energy */

    if (signal < 1e-9) {
        for (order = 0; order <= fOrder; order++) kArray[order] = (order == 0) ? 1.0 : 0.0;
        return 0;
    }

    /* Set up iteration state */
    kArray[0]=1.0;
    aPtr[0]=1.0;        /* Ptr to predictor coeffs, current iteration*/
    aLastPtr[0]=1.0;    /* Ptr to predictor coeffs, last iteration */
    error=rArray[0];

    /* Now perform recursion */
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

        /* Now make current iteration the last one */
        aTemp=aLastPtr;
        aLastPtr=aPtr;      /* Current becomes last */
        aPtr=aTemp;         /* Last becomes current */
    }
    return signal/error;    /* return the gain */
}


/*****************************************************/
/* StepUp:                                           */
/*   Convert reflection coefficients into            */
/*   predictor coefficients.                         */
/*****************************************************/
static void StepUp(int fOrder,faac_real* kArray,faac_real* aArray)
{
    faac_real aTemp[TNS_MAX_ORDER+2];
    int i,order;

    aArray[0]=1.0;
    aTemp[0]=1.0;
    for (order=1;order<=fOrder;order++) {
        aArray[order]=0.0;
        for (i=1;i<=order;i++) {
            aTemp[i] = aArray[i] + kArray[order]*aArray[order-i];
        }
        for (i=1;i<=order;i++) {
            aArray[i]=aTemp[i];
        }
    }
}
