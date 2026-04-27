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
/*
 * $Id: tns.c,v 1.11 2012/03/01 18:34:17 knik Exp $
 */

#include <math.h>
#include "frame.h"
#include "coder.h"
#include "bitstream.h"
#include "tns.h"
#include "util.h"

/***********************************************/
/* TNS Profile/Frequency Dependent Parameters  */
/***********************************************/
/* Shifted to approx 1.3 kHz based on instrumentation hints */
static unsigned short tnsMinBandNumberLong[12] =
{ 7, 8, 10, 11, 12, 14, 17, 18, 17, 20, 21, 22 };
static unsigned short tnsMinBandNumberShort[12] =
{ 1, 1, 1, 2, 2, 3, 4, 4, 6, 7, 7, 8 };

/**************************************/
/* Main/Low Profile TNS Parameters    */
/**************************************/
static unsigned short tnsMaxBandsLongMainLow[12] =
{ 31, 31, 34, 40, 42, 51, 46, 46, 42, 42, 42, 39 };

static unsigned short tnsMaxBandsShortMainLow[12] =
{ 9, 9, 10, 14, 14, 14, 14, 14, 14, 14, 14, 14 };

/* Capped to reduce bit budget consumption at low bitrates */
static unsigned short tnsMaxOrderLongMain = 12;
static unsigned short tnsMaxOrderLongLow = 12;
static unsigned short tnsMaxOrderShortMainLow = 5;


/*************************/
/* Function prototypes   */
/*************************/
static void Autocorrelation(int maxOrder,        /* Maximum autocorr order */
                     int dataSize,        /* Size of the data array */
                     const faac_real* data,        /* Data array */
                     faac_real* rArray,
                     const faac_real* acfWin);     /* Autocorrelation array */

static faac_real LevinsonDurbin(int maxOrder,        /* Maximum filter order */
                      int dataSize,        /* Size of the data array */
                      const faac_real* data,        /* Data array */
                      faac_real* kArray,
                      const faac_real* acfWin);     /* Reflection coeff array */

static void StepUp(int fOrder, faac_real* kArray, faac_real* aArray);

static void QuantizeReflectionCoeffs(int fOrder,int coeffRes,faac_real* rArray,int* indexArray);
static int TruncateCoeffs(int fOrder,faac_real threshold,faac_real* kArray);
static void TnsInvFilter(int length,faac_real* spec,TnsFilterData* filter, faac_real *temp);

static void CalcGaussWindow(faac_real *win, int winSize, int samplingRate, int blockType, double timeResolution)
{
    int i;
    double gaussExp = M_PI * samplingRate * 0.001 * timeResolution / (blockType != ONLY_SHORT_WINDOW ? 1024.0 : 128.0);
    gaussExp = -0.5 * gaussExp * gaussExp;

    for (i = 0; i < winSize; i++) {
        win[i] = (faac_real)exp(gaussExp * (i + 0.5) * (i + 0.5));
    }
}

static void CalcWeightedSpectrum(const faac_real spectrum[],
                                 faac_real weightedSpectrum[],
                                 int numberOfBands,
                                 const int* sfbOffset,
                                 int lpcStartBand,
                                 int lpcStopBand)
{
    int i, sfb;
    faac_real tnsSfbMean[NSFB_LONG];
    faac_real tmp;

    for (sfb = lpcStartBand; sfb < lpcStopBand; sfb++) {
        faac_real energy = 1e-30;
        for (i = sfbOffset[sfb]; i < sfbOffset[sfb+1]; i++) {
            energy += spectrum[i] * spectrum[i];
        }
        tnsSfbMean[sfb] = (faac_real)(1.0 / sqrt(energy));
    }

    sfb = lpcStartBand;
    tmp = tnsSfbMean[sfb];
    for (i = sfbOffset[lpcStartBand]; i < sfbOffset[lpcStopBand]; i++) {
        if (sfb + 1 < lpcStopBand && sfbOffset[sfb+1] == i) {
            sfb++;
            tmp = tnsSfbMean[sfb];
        }
        weightedSpectrum[i] = tmp;
    }

    /* Filter down */
    for (i = sfbOffset[lpcStopBand] - 2; i >= sfbOffset[lpcStartBand]; i--) {
        weightedSpectrum[i] = (weightedSpectrum[i] + weightedSpectrum[i+1]) * (faac_real)0.5;
    }
    /* Filter up */
    for (i = sfbOffset[lpcStartBand] + 1; i < sfbOffset[lpcStopBand]; i++) {
        weightedSpectrum[i] = (weightedSpectrum[i] + weightedSpectrum[i-1]) * (faac_real)0.5;
    }

    /* Weight spectrum */
    for (i = sfbOffset[lpcStartBand]; i < sfbOffset[lpcStopBand]; i++) {
        weightedSpectrum[i] *= spectrum[i];
    }
}


/*****************************************************/
/* InitTns:                                          */
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
                tnsInfo->tnsMaxOrderLong = 12;
            }
            tnsInfo->tnsMaxOrderShort = tnsMaxOrderShortMainLow;
            break;
        case LOW :
            tnsInfo->tnsMaxBandsLong = tnsMaxBandsLongMainLow[fsIndex];
            tnsInfo->tnsMaxBandsShort = tnsMaxBandsShortMainLow[fsIndex];
            if (hEncoder->config.mpegVersion == 1) { /* MPEG2 */
                tnsInfo->tnsMaxOrderLong = tnsMaxOrderLongLow;
            } else { /* MPEG4 */
                tnsInfo->tnsMaxOrderLong = 12;
            }
            tnsInfo->tnsMaxOrderShort = tnsMaxOrderShortMainLow;
            break;
        }
        tnsInfo->tnsMinBandNumberLong = tnsMinBandNumberLong[fsIndex];
        tnsInfo->tnsMinBandNumberShort = tnsMinBandNumberShort[fsIndex];

        CalcGaussWindow(tnsInfo->acfWindowLong, TNS_MAX_ORDER + 1, hEncoder->sampleRate, ONLY_LONG_WINDOW, 0.75);
        CalcGaussWindow(tnsInfo->acfWindowShort, TNS_MAX_ORDER + 1, hEncoder->sampleRate, ONLY_SHORT_WINDOW, 0.75);
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
               faac_real* temp,
               int bitRatePerChannel)
{
    int numberOfWindows,windowSize;
    int startBand,stopBand,order;    /* Bands over which to apply TNS */
    int lengthInBands;               /* Length to filter, in bands */
    int w;
    int startIndex,length;
    faac_real gain;
    faac_real weightedSpec[FRAME_LEN];
    faac_real *acfWin;

    switch( blockType ) {
    case ONLY_SHORT_WINDOW :
        numberOfWindows = MAX_SHORT_WINDOWS;
        windowSize = BLOCK_LEN_SHORT;
        startBand = tnsInfo->tnsMinBandNumberShort;
        stopBand = numberOfBands;
        lengthInBands = stopBand-startBand;
        order = tnsInfo->tnsMaxOrderShort;
        startBand = min(startBand,tnsInfo->tnsMaxBandsShort);
        stopBand = min(stopBand,tnsInfo->tnsMaxBandsShort);
        acfWin = tnsInfo->acfWindowShort;
        break;

    default:
        numberOfWindows = 1;
        windowSize = BLOCK_LEN_LONG;
        startBand = tnsInfo->tnsMinBandNumberLong;
        stopBand = numberOfBands;
        lengthInBands = stopBand - startBand;
        order = tnsInfo->tnsMaxOrderLong;
        startBand = min(startBand,tnsInfo->tnsMaxBandsLong);
        stopBand = min(stopBand,tnsInfo->tnsMaxBandsLong);
        acfWin = tnsInfo->acfWindowLong;
        break;
    }

    /* Make sure that start and stop bands < maxSfb */
    /* Make sure that start and stop bands >= 0 */
    startBand = min(startBand,maxSfb);
    stopBand = min(stopBand,maxSfb);
    startBand = max(startBand,0);
    stopBand = max(stopBand,0);

    tnsInfo->tnsDataPresent = 0;     /* default TNS not used */

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

        CalcWeightedSpectrum(&spec[w * windowSize], weightedSpec, numberOfBands, sfbOffsetTable, startBand, stopBand);

        gain = LevinsonDurbin(order,length,&weightedSpec[sfbOffsetTable[startBand]],k,acfWin);

        faac_real thresh = (faac_real)DEF_TNS_GAIN_THRESH;
        if (bitRatePerChannel < 16000) thresh = (faac_real)2.4;
        else if (bitRatePerChannel < 20000) thresh = (faac_real)2.6;
        else thresh = (faac_real)2.8;

        if (gain > thresh) {  /* Use TNS */
            int truncatedOrder;
            windowData->numFilters++;
            tnsInfo->tnsDataPresent=1;
            tnsFilter->direction = 0;
            tnsFilter->coefCompress = 0;
            tnsFilter->length = lengthInBands;
            QuantizeReflectionCoeffs(order,DEF_TNS_COEFF_RES,k,tnsFilter->index);
            truncatedOrder = TruncateCoeffs(order,DEF_TNS_COEFF_THRESH,k);
            tnsFilter->order = truncatedOrder;
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
        stopBand = numberOfBands;
        startBand = min(startBand,tnsInfo->tnsMaxBandsShort);
        stopBand = min(stopBand,tnsInfo->tnsMaxBandsShort);
        break;

    default:
        numberOfWindows = 1;
        windowSize = BLOCK_LEN_LONG;
        startBand = tnsInfo->tnsMinBandNumberLong;
        stopBand = numberOfBands;
        startBand = min(startBand,tnsInfo->tnsMaxBandsLong);
        stopBand = min(stopBand,tnsInfo->tnsMaxBandsLong);
        break;
    }

    /* Make sure that start and stop bands < maxSfb */
    /* Make sure that start and stop bands >= 0 */
    startBand = min(startBand,maxSfb);
    stopBand = min(stopBand,maxSfb);
    startBand = max(startBand,0);
    stopBand = max(stopBand,0);


    /* Perform filtering for each window */
    for(w=0;w<numberOfWindows;w++)
    {
        TnsWindowData* windowData = &tnsInfo->windowData[w];
        TnsFilterData* tnsFilter = windowData->tnsFilter;

        startIndex = w * windowSize + sfbOffsetTable[startBand];
        length = sfbOffsetTable[stopBand] - sfbOffsetTable[startBand];

        if (tnsInfo->tnsDataPresent  &&  windowData->numFilters) {  /* Use TNS */
            TnsInvFilter(length,&spec[startIndex],tnsFilter,temp);
        }
    }
}




/********************************************************/
/* TnsInvFilter:                                        */
/*   Inverse filter the given spec with specified       */
/*   length using the coefficients specified in filter. */
/*   Not that the order and direction are specified     */
/*   withing the TNS_FILTER_DATA structure.             */
/********************************************************/
static void TnsInvFilter(int length,faac_real* spec,TnsFilterData* filter, faac_real *temp)
{
    int i,j,k=0;
    int order=filter->order;
    faac_real* a=filter->aCoeffs;

    /* Determine loop parameters for given direction */
    if (filter->direction) {

        /* Startup, initial state is zero */
        temp[length-1]=spec[length-1];
        for (i=length-2;i>(length-1-order);i--) {
            temp[i]=spec[i];
            k++;
            for (j=1;j<=k;j++) {
                spec[i]+=temp[i+j]*a[j];
            }
        }

        /* Now filter the rest */
        for (i=length-1-order;i>=0;i--) {
            temp[i]=spec[i];
            for (j=1;j<=order;j++) {
                spec[i]+=temp[i+j]*a[j];
            }
        }


    } else {

        /* Startup, initial state is zero */
        temp[0]=spec[0];
        for (i=1;i<order;i++) {
            temp[i]=spec[i];
            for (j=1;j<=i;j++) {
                spec[i]+=temp[i-j]*a[j];
            }
        }

        /* Now filter the rest */
        for (i=order;i<length;i++) {
            temp[i]=spec[i];
            for (j=1;j<=order;j++) {
                spec[i]+=temp[i-j]*a[j];
            }
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

    for (i = fOrder; i >= 0; i--) {
        kArray[i] = (FAAC_FABS(kArray[i])>threshold) ? kArray[i] : 0.0;
        if (kArray[i]!=0.0) return i;
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
                              faac_real* kArray,
                              int* indexArray)
{
    faac_real iqfac,iqfac_m;
    int i;

    iqfac = ((1<<(coeffRes-1))-0.5)/(M_PI/2);
    iqfac_m = ((1<<(coeffRes-1))+0.5)/(M_PI/2);

    /* Quantize and inverse quantize */
    for (i=1;i<=fOrder;i++) {
        indexArray[i] = (kArray[i]>=0)?(int)(0.5+(FAAC_ASIN(kArray[i])*iqfac)):(int)(-0.5+(FAAC_ASIN(kArray[i])*iqfac_m));
        kArray[i] = FAAC_SIN((faac_real)indexArray[i]/((indexArray[i]>=0)?iqfac:iqfac_m));
    }
}

/*****************************************************/
/* Autocorrelation,                                  */
/*   Compute the autocorrelation function            */
/*   estimate for the given data.                    */
/*****************************************************/
static void Autocorrelation(int maxOrder,        /* Maximum autocorr order */
                     int dataSize,        /* Size of the data array */
                     const faac_real* data,        /* Data array */
                     faac_real* rArray,
                     const faac_real* acfWin)      /* Autocorrelation array */
{
    int order,index;

    for (order=0;order<=maxOrder;order++) {
        faac_real accu = 0.0;
        for (index=0;index<dataSize;index++) {
            accu += data[index]*data[index+order];
        }
        if (acfWin)
            rArray[order] = accu * acfWin[order];
        else
            rArray[order] = accu;
        dataSize--;
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
                      const faac_real* data,        /* Data array */
                      faac_real* kArray,
                      const faac_real* acfWin)      /* Reflection coeff array */
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
    Autocorrelation(fOrder,dataSize,data,rArray,acfWin);
    signal=rArray[0];   /* signal energy */

    /* Set up pointers to current and last iteration */
    /* predictor coefficients.                       */
    aPtr = aArray1;
    aLastPtr = aArray2;
    /* If there is no signal energy, return */
    if (!signal) {
        kArray[0]=1.0;
        for (order=1;order<=fOrder;order++) {
            kArray[order]=0.0;
        }
        return 0;

    } else {

        /* Set up first iteration */
        kArray[0]=1.0;
        aPtr[0]=1.0;        /* Ptr to predictor coeffs, current iteration*/
        aLastPtr[0]=1.0;    /* Ptr to predictor coeffs, last iteration */
        error=rArray[0];

        /* Now perform recursion */
        for (order=1;order<=fOrder;order++) {
            kTemp = aLastPtr[0]*rArray[order-0];
            for (i=1;i<order;i++) {
                kTemp += aLastPtr[i]*rArray[order-i];
            }
            if (error <= 1e-30 || FAAC_FABS(kTemp) >= error) {
                error = 0.0;
                break;
            }
            kTemp = -kTemp/error;
            kArray[order]=kTemp;
            aPtr[order]=kTemp;
            for (i=1;i<order;i++) {
                aPtr[i] = aLastPtr[i] + kTemp*aLastPtr[order-i];
            }
            error = error * (1 - kTemp*kTemp);
            if (error <= 1e-30) break;

            /* Now make current iteration the last one */
            aTemp=aLastPtr;
            aLastPtr=aPtr;      /* Current becomes last */
            aPtr=aTemp;         /* Last becomes current */
        }
        /* If perfect prediction, trigger TNS */
        if (error <= 1e-30) return DEF_TNS_GAIN_THRESH + 1.0;
        return signal/error;    /* return the gain */
    }
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
