/**********************************************************************

This software module was originally developed by
and edited by Texas Instruments in the course of
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "coder.h"
#include "channels.h"
#include "huff2.h"
#include "bitstream.h"
#include "util.h"
#include "sbr.h"

static int CountBitstream(faacEncStruct* hEncoder,
                          CoderInfo *coderInfo,
                          ChannelInfo *channelInfo,
                          BitStream *bitStream,
                          int numChannels);
static int WriteADTSHeader(faacEncStruct* hEncoder,
                           BitStream *bitStream,
                           int writeFlag);
static int WriteCPE(CoderInfo *coderInfoL,
                    CoderInfo *coderInfoR,
                    ChannelInfo *channelInfo,
                    BitStream* bitStream,
                    int objectType,
                    int writeFlag);
static int WriteSCE(CoderInfo *coderInfo,
                    ChannelInfo *channelInfo,
                    BitStream *bitStream,
                    int objectType,
                    int writeFlag);
static int WriteLFE(CoderInfo *coderInfo,
                    ChannelInfo *channelInfo,
                    BitStream *bitStream,
                    int objectType,
                    int writeFlag);
static int WriteICSInfo(CoderInfo *coderInfo,
                        BitStream *bitStream,
                        int objectType,
                        int common_window,
                        int writeFlag);
static int WriteICS(CoderInfo *coderInfo,
                    BitStream *bitStream,
                    int commonWindow,
                    int objectType,
                    int writeFlag);
static int WritePulseData(CoderInfo *coderInfo,
                          BitStream *bitStream,
                          int writeFlag);
static int WriteTNSData(CoderInfo *coderInfo,
                        BitStream *bitStream,
                        int writeFlag);
static int WriteGainControlData(CoderInfo *coderInfo,
                                BitStream *bitStream,
                                int writeFlag);
static int WriteSpectralData(CoderInfo *coderInfo,
                             BitStream *bitStream,
                             int writeFlag);
static int WriteAACFillBits(BitStream* bitStream,
                            int numBits,
                            int writeFlag);
static int FindGroupingBits(CoderInfo *coderInfo);
static long BufferNumBit(BitStream *bitStream);
static int ByteAlign(BitStream* bitStream,
                     int writeFlag, int bitsSoFar);

int WriteBitstream(faacEncStruct* hEncoder,
                   CoderInfo *coderInfo,
                   ChannelInfo *channelInfo,
                   BitStream *bitStream,
                   int numChannel)
{
    int channel;
    int bits = 0;
    int bitsLeftAfterFill, numFillBits;

    if (CountBitstream(hEncoder, coderInfo, channelInfo, bitStream, numChannel) < 0)
        return -1;

    if(hEncoder->config.outputFormat == 1){
        bits += WriteADTSHeader(hEncoder, bitStream, 1);
    }else{
        bits = 0;
    }

    for (channel = 0; channel < numChannel; channel++) {

        if (channelInfo[channel].present) {

            /* Write out a single_channel_element */
            if (channelInfo[channel].type != ELEMENT_CPE) {

                if (channelInfo[channel].type == ELEMENT_LFE) {
                    /* Write out lfe */
                    bits += WriteLFE(&coderInfo[channel],
                        &channelInfo[channel],
                        bitStream,
                        hEncoder->config.aacObjectType,
                        1);
                } else {
                    /* Write out sce */
                    bits += WriteSCE(&coderInfo[channel],
                        &channelInfo[channel],
                        bitStream,
                        hEncoder->config.aacObjectType,
                        1);
                }

            } else {

                if (channelInfo[channel].ch_is_left) {
                    /* Write out cpe */
                    bits += WriteCPE(&coderInfo[channel],
                        &coderInfo[channelInfo[channel].paired_ch],
                        &channelInfo[channel],
                        bitStream,
                        hEncoder->config.aacObjectType,
                        1);
                }
            }
        }
    }

    /* Compute how many fill bits are needed to avoid overflowing bit reservoir */
    /* Save room for ID_END terminator */
    if (bits < (8 - LEN_SE_ID) ) {
        numFillBits = 8 - LEN_SE_ID - bits;
    } else {
        numFillBits = 0;
    }

    /* Write AAC fill_elements, smallest fill element is 7 bits. */
    /* Function may leave up to 6 bits left after fill, so tell it to fill a few extra */
    numFillBits += 6;
    bitsLeftAfterFill = WriteAACFillBits(bitStream, numFillBits, 1);
    bits += (numFillBits - bitsLeftAfterFill);

    /* Write SBR extension payload for HE-AAC (fill element with EXT_SBR_DATA) */
    if (hEncoder->config.aacObjectType == HE_AAC && hEncoder->sbrInfo) {
        int id_aac = (numChannel > 1) ? ID_CPE : ID_SCE;
        bits += SBRWriteBitstream(hEncoder->sbrInfo, bitStream, id_aac, 1);
    }

    /* Write ID_END terminator */
    bits += LEN_SE_ID;
    PutBit(bitStream, ID_END, LEN_SE_ID);

    /* Now byte align the bitstream */
    bits += ByteAlign(bitStream, 1, bits);

    return bits;
}

static int CountBitstream(faacEncStruct* hEncoder,
                          CoderInfo *coderInfo,
                          ChannelInfo *channelInfo,
                          BitStream *bitStream,
                          int numChannel)
{
    int channel;
    int bits = 0;
    int bitsLeftAfterFill, numFillBits;

    if(hEncoder->config.outputFormat == 1){
        bits += WriteADTSHeader(hEncoder, bitStream, 0);
    }else{
        bits = 0;
    }

    for (channel = 0; channel < numChannel; channel++) {

        if (channelInfo[channel].present) {

            /* Write out a single_channel_element */
            if (channelInfo[channel].type != ELEMENT_CPE) {

                if (channelInfo[channel].type == ELEMENT_LFE) {
                    /* Write out lfe */
                    bits += WriteLFE(&coderInfo[channel],
                        &channelInfo[channel],
                        bitStream,
                        hEncoder->config.aacObjectType,
                        0);
                } else {
                    /* Write out sce */
                    bits += WriteSCE(&coderInfo[channel],
                        &channelInfo[channel],
                        bitStream,
                        hEncoder->config.aacObjectType,
                        0);
                }

            } else {

                if (channelInfo[channel].ch_is_left) {
                    /* Write out cpe */
                    bits += WriteCPE(&coderInfo[channel],
                        &coderInfo[channelInfo[channel].paired_ch],
                        &channelInfo[channel],
                        bitStream,
                        hEncoder->config.aacObjectType,
                        0);
                }
            }
        }
    }

    /* Compute how many fill bits are needed to avoid overflowing bit reservoir */
    /* Save room for ID_END terminator */
    if (bits < (8 - LEN_SE_ID) ) {
        numFillBits = 8 - LEN_SE_ID - bits;
    } else {
        numFillBits = 0;
    }

    /* Write AAC fill_elements, smallest fill element is 7 bits. */
    /* Function may leave up to 6 bits left after fill, so tell it to fill a few extra */
    numFillBits += 6;
    bitsLeftAfterFill = WriteAACFillBits(bitStream, numFillBits, 0);
    bits += (numFillBits - bitsLeftAfterFill);

    /* Count SBR extension payload for HE-AAC */
    if (hEncoder->config.aacObjectType == HE_AAC && hEncoder->sbrInfo) {
        int id_aac = (numChannel > 1) ? ID_CPE : ID_SCE;
        bits += SBRWriteBitstream(hEncoder->sbrInfo, NULL, id_aac, 0);
    }

    /* Write ID_END terminator */
    bits += LEN_SE_ID;

    /* Now byte align the bitstream */
    bits += ByteAlign(bitStream, 0, bits);

    hEncoder->usedBytes = bit2byte(bits);

    if (hEncoder->usedBytes > bitStream->size)
    {
        fprintf(stderr, "frame buffer overrun\n");
        return -1;
    }
    if (hEncoder->usedBytes >= ADTS_FRAMESIZE)
    {
        fprintf(stderr, "frame size limit exceeded\n");
        return -1;
    }

    return bits;
}

static int WriteADTSHeader(faacEncStruct* hEncoder,
                           BitStream *bitStream,
                           int writeFlag)
{
    int bits = 56;

    if (writeFlag) {
        /* Fixed ADTS header */
        PutBit(bitStream, 0xFFFF, 12); /* 12 bit Syncword */
        PutBit(bitStream, hEncoder->config.mpegVersion, 1); /* ID == 0 for MPEG4 AAC, 1 for MPEG2 AAC */
        PutBit(bitStream, 0, 2); /* layer == 0 */
        PutBit(bitStream, 1, 1); /* protection absent */
        /* ADTS profile: always AAC-LC (1) for both LC and HE-AAC.
         * HE-AAC core is AAC-LC at Fs/2; SBR data lives in fill elements. */
        int adts_profile = (hEncoder->config.aacObjectType == HE_AAC) ? LOW - 1
                                                                       : hEncoder->config.aacObjectType - 1;
        PutBit(bitStream, adts_profile, 2); /* profile */
        PutBit(bitStream, hEncoder->sampleRateIdx, 4); /* sampling rateIdx */
        PutBit(bitStream, 0, 1); /* private bit */
        PutBit(bitStream, hEncoder->numChannels, 3); /* ch. config */
        PutBit(bitStream, 0, 1); /* original/copy */
        PutBit(bitStream, 0, 1); /* home */

        /* Variable ADTS header */
        PutBit(bitStream, 0, 1); /* copyr. id. bit */
        PutBit(bitStream, 0, 1); /* copyr. id. start */
        PutBit(bitStream, hEncoder->usedBytes, 13);
        PutBit(bitStream, 0x7FF, 11); /* buffer fullness */
        PutBit(bitStream, 0, 2); /* raw data blocks */

    }

    return bits;
}

static int WriteCPE(CoderInfo *coderInfoL,
                    CoderInfo *coderInfoR,
                    ChannelInfo *channelInfo,
                    BitStream* bitStream,
                    int objectType,
                    int writeFlag)
{
    int bits = 0;

    if (writeFlag) {
        PutBit(bitStream, ID_CPE, LEN_SE_ID);
        PutBit(bitStream, channelInfo->tag, LEN_TAG);
        PutBit(bitStream, channelInfo->common_window, LEN_COM_WIN);
    }

    bits += LEN_SE_ID;
    bits += LEN_TAG;
    bits += LEN_COM_WIN;

    if (channelInfo->common_window) {
        int numWindows, maxSfb;
        bits += WriteICSInfo(coderInfoL, bitStream, objectType, channelInfo->common_window, writeFlag);
        numWindows = coderInfoL->groups.n;
        maxSfb = coderInfoL->sfbn;

        if (writeFlag) {
            PutBit(bitStream, channelInfo->msInfo.is_present, LEN_MASK_PRES);
            if (channelInfo->msInfo.is_present == 1) {
                int g, b;
                for (g=0;g<numWindows;g++) {
                    for (b=0;b<maxSfb;b++) {
                        PutBit(bitStream, channelInfo->msInfo.ms_used[g*maxSfb+b], LEN_MASK);
                    }
                }
            }
        }
        bits += LEN_MASK_PRES;
        if (channelInfo->msInfo.is_present == 1)
            bits += (numWindows*maxSfb*LEN_MASK);
    }

    bits += WriteICS(coderInfoL, bitStream, channelInfo->common_window, objectType, writeFlag);
    bits += WriteICS(coderInfoR, bitStream, channelInfo->common_window, objectType, writeFlag);

    return bits;
}

static int WriteSCE(CoderInfo *coderInfo,
                    ChannelInfo *channelInfo,
                    BitStream *bitStream,
                    int objectType,
                    int writeFlag)
{
    int bits = 0;
    if (writeFlag) {
        PutBit(bitStream, ID_SCE, LEN_SE_ID);
        PutBit(bitStream, channelInfo->tag, LEN_TAG);
    }
    bits += LEN_SE_ID;
    bits += LEN_TAG;
    bits += WriteICS(coderInfo, bitStream, 0, objectType, writeFlag);
    return bits;
}

static int WriteLFE(CoderInfo *coderInfo,
                    ChannelInfo *channelInfo,
                    BitStream *bitStream,
                    int objectType,
                    int writeFlag)
{
    int bits = 0;
    if (writeFlag) {
        PutBit(bitStream, ID_LFE, LEN_SE_ID);
        PutBit(bitStream, channelInfo->tag, LEN_TAG);
    }
    bits += LEN_SE_ID;
    bits += LEN_TAG;
    bits += WriteICS(coderInfo, bitStream, 0, objectType, writeFlag);
    return bits;
}

static int WriteICSInfo(CoderInfo *coderInfo,
                        BitStream *bitStream,
                        int objectType,
                        int common_window,
                        int writeFlag)
{
    int grouping_bits;
    int bits = 0;

    if (writeFlag) {
        PutBit(bitStream, 0, LEN_ICS_RESERV);
        PutBit(bitStream, coderInfo->block_type, LEN_WIN_SEQ);
        PutBit(bitStream, coderInfo->window_shape, LEN_WIN_SH);
    }
    bits += LEN_ICS_RESERV;
    bits += LEN_WIN_SEQ;
    bits += LEN_WIN_SH;

    if (coderInfo->block_type == ONLY_SHORT_WINDOW){
        if (writeFlag) {
            PutBit(bitStream, coderInfo->sfbn, LEN_MAX_SFBS);
            grouping_bits = FindGroupingBits(coderInfo);
            PutBit(bitStream, grouping_bits, MAX_SHORT_WINDOWS - 1);
        }
        bits += LEN_MAX_SFBS;
        bits += MAX_SHORT_WINDOWS - 1;
    } else {
        if (writeFlag) {
            PutBit(bitStream, coderInfo->sfbn, LEN_MAX_SFBL);
            PutBit(bitStream, 0, LEN_PRED_PRES);
        }
        bits += LEN_MAX_SFBL + LEN_PRED_PRES;
    }

    return bits;
}

static int WriteICS(CoderInfo *coderInfo,
                    BitStream *bitStream,
                    int commonWindow,
                    int objectType,
                    int writeFlag)
{
    int bits = 0;
    if (writeFlag)
        PutBit(bitStream, coderInfo->global_gain, LEN_GLOB_GAIN);
    bits += LEN_GLOB_GAIN;

    if (!commonWindow) {
        bits += WriteICSInfo(coderInfo, bitStream, objectType, commonWindow, writeFlag);
    }

    bits += writebooks(coderInfo, bitStream, writeFlag);
    bits += writesf(coderInfo, bitStream, writeFlag);
    bits += WritePulseData(coderInfo, bitStream, writeFlag);
    bits += WriteTNSData(coderInfo, bitStream, writeFlag);
    bits += WriteGainControlData(coderInfo, bitStream, writeFlag);
    bits += WriteSpectralData(coderInfo, bitStream, writeFlag);

    return bits;
}

static int WritePulseData(CoderInfo *coderInfo,
                          BitStream *bitStream,
                          int writeFlag)
{
    if (writeFlag) PutBit(bitStream, 0, LEN_PULSE_PRES);
    return LEN_PULSE_PRES;
}

static int WriteTNSData(CoderInfo *coderInfo,
                        BitStream *bitStream,
                        int writeFlag)
{
    int bits = 0;
    int numWindows, len_tns_nfilt, len_tns_length, len_tns_order;
    int filtNumber, resInBits, bitsToTransmit, w;
    unsigned long unsignedIndex;
    TnsInfo* tnsInfoPtr = &coderInfo->tnsInfo;

    if (writeFlag) PutBit(bitStream,tnsInfoPtr->tnsDataPresent,LEN_TNS_PRES);
    bits += LEN_TNS_PRES;

    if (!tnsInfoPtr->tnsDataPresent) return bits;

    if (coderInfo->block_type == ONLY_SHORT_WINDOW) {
        numWindows = MAX_SHORT_WINDOWS;
        len_tns_nfilt = LEN_TNS_NFILTS;
        len_tns_length = LEN_TNS_LENGTHS;
        len_tns_order = LEN_TNS_ORDERS;
    } else {
        numWindows = 1;
        len_tns_nfilt = LEN_TNS_NFILTL;
        len_tns_length = LEN_TNS_LENGTHL;
        len_tns_order = LEN_TNS_ORDERL;
    }

    for (w=0;w<numWindows;w++) {
        TnsWindowData* windowDataPtr = &tnsInfoPtr->windowData[w];
        int numFilters = windowDataPtr->numFilters;
        if (writeFlag) PutBit(bitStream,numFilters,len_tns_nfilt);
        bits += len_tns_nfilt;
        if (numFilters) {
            resInBits = windowDataPtr->coefResolution;
            if (writeFlag) PutBit(bitStream,resInBits-DEF_TNS_RES_OFFSET,LEN_TNS_COEFF_RES);
            bits += LEN_TNS_COEFF_RES;
            for (filtNumber=0;filtNumber<numFilters;filtNumber++) {
                TnsFilterData* tnsFilterPtr=&windowDataPtr->tnsFilter[filtNumber];
                int order = tnsFilterPtr->order;
                if (writeFlag) {
                    PutBit(bitStream,tnsFilterPtr->length,len_tns_length);
                    PutBit(bitStream,order,len_tns_order);
                }
                bits += len_tns_length + len_tns_order;
                if (order) {
                    if (writeFlag) {
                        PutBit(bitStream,tnsFilterPtr->direction,LEN_TNS_DIRECTION);
                        PutBit(bitStream,tnsFilterPtr->coefCompress,LEN_TNS_COMPRESS);
                    }
                    bits += (LEN_TNS_DIRECTION + LEN_TNS_COMPRESS);
                    bitsToTransmit = resInBits - tnsFilterPtr->coefCompress;
                    bits += order * bitsToTransmit;
                    if (writeFlag) {
                        int i;
                        for (i=1;i<=order;i++) {
                            unsignedIndex = (unsigned long) (tnsFilterPtr->index[i])&(~(~0<<bitsToTransmit));
                            PutBit(bitStream,unsignedIndex,bitsToTransmit);
                        }
                    }
                }
            }
        }
    }
    return bits;
}

static int WriteGainControlData(CoderInfo *coderInfo,
                                BitStream *bitStream,
                                int writeFlag)
{
    if (writeFlag) PutBit(bitStream, 0, LEN_GAIN_PRES);
    return LEN_GAIN_PRES;
}

static int WriteSpectralData(CoderInfo *coderInfo,
                             BitStream *bitStream,
                             int writeFlag)
{
    int i, bits = 0;
    if (writeFlag) {
        for(i = 0; i < coderInfo->datacnt; i++) {
            int data = coderInfo->s[i].data;
            int len = coderInfo->s[i].len;
            if (len > 0) {
                PutBit(bitStream, data, len);
                bits += len;
            }
        }
    } else {
        for(i = 0; i < coderInfo->datacnt; i++) bits += coderInfo->s[i].len;
    }
    return bits;
}

static int WriteAACFillBits(BitStream* bitStream,
                            int numBits,
                            int writeFlag)
{
    int numberOfBitsLeft = numBits;
    int minNumberOfBits = LEN_SE_ID + LEN_F_CNT;
    while (numberOfBitsLeft >= minNumberOfBits)
    {
        int numberOfBytes, maxCount;
        if (writeFlag) PutBit(bitStream, ID_FIL, LEN_SE_ID);
        numberOfBitsLeft -= minNumberOfBits;
        numberOfBytes = (int)(numberOfBitsLeft/LEN_BYTE);
        maxCount = (1<<LEN_F_CNT) - 1;
        if (numberOfBytes < maxCount) {
            if (writeFlag) {
                PutBit(bitStream, numberOfBytes, LEN_F_CNT);
                for (int i = 0; i < numberOfBytes; i++) PutBit(bitStream, 0, LEN_BYTE);
            }
        } else {
            int maxEscapeCount = (1<<LEN_BYTE) - 1;
            int maxNumberOfBytes = maxCount + maxEscapeCount;
            numberOfBytes = (numberOfBytes > maxNumberOfBytes ) ? (maxNumberOfBytes) : (numberOfBytes);
            int escCount = numberOfBytes - maxCount;
            if (writeFlag) {
                PutBit(bitStream, maxCount, LEN_F_CNT);
                PutBit(bitStream, escCount, LEN_BYTE);
                for (int i = 0; i < numberOfBytes-1; i++) PutBit(bitStream, 0, LEN_BYTE);
            }
        }
        numberOfBitsLeft -= LEN_BYTE*numberOfBytes;
    }
    return numberOfBitsLeft;
}

static int FindGroupingBits(CoderInfo *coderInfo)
{
    int grouping_bits = 0, tmp[8], index = 0;
    for(int i = 0; i < coderInfo->groups.n; i++)
        for (int j = 0; j < coderInfo->groups.len[i]; j++) tmp[index++] = i;
    for(int i = 1; i < 8; i++){
        grouping_bits <<= 1;
        if(tmp[i] == tmp[i-1]) grouping_bits++;
    }
    return grouping_bits;
}

BitStream *OpenBitStream(int size, unsigned char *buffer)
{
    BitStream *bitStream = AllocMemory(sizeof(BitStream));
    bitStream->size = size;
    bitStream->numBit = 0;
    bitStream->currentBit = 0;
    bitStream->data = buffer;
    SetMemory(bitStream->data, 0, size);
    return bitStream;
}

int CloseBitStream(BitStream *bitStream)
{
    int bytes = bit2byte(bitStream->numBit);
    FreeMemory(bitStream);
    return bytes;
}

static long BufferNumBit(BitStream *bitStream)
{
    return bitStream->numBit;
}

int PutBit(BitStream *bitStream,
           unsigned long data,
           int numBit)
{
    if (numBit == 0) return 0;
    unsigned int currentBit = (unsigned int)bitStream->currentBit;
    unsigned int bitOffset = currentBit & 7;
    unsigned char *ptr = bitStream->data + (currentBit >> 3);
    bitStream->currentBit += numBit;
    bitStream->numBit = bitStream->currentBit;
    data &= (1UL << numBit) - 1;
    if (bitOffset + numBit <= 8) {
        if (bitOffset == 0) *ptr = 0;
        *ptr |= (unsigned char)(data << (8 - bitOffset - numBit));
    } else {
        int firstBits = 8 - bitOffset;
        if (bitOffset == 0) *ptr = 0;
        *ptr++ |= (unsigned char)(data >> (numBit - firstBits));
        numBit -= firstBits;
        while (numBit >= 8) {
            *ptr++ = (unsigned char)((data >> (numBit - 8)) & 0xFF);
            numBit -= 8;
        }
        if (numBit > 0) {
            *ptr = (unsigned char)((data & ((1UL << numBit) - 1)) << (8 - numBit));
        }
    }
    return 0;
}

static int ByteAlign(BitStream *bitStream, int writeFlag, int bitsSoFar)
{
    int len = writeFlag ? BufferNumBit(bitStream) : bitsSoFar;
    int j = (8 - (len%8))%8;
    if (writeFlag) for( int i=0; i<j; i++ ) PutBit(bitStream, 0, 1);
    return j;
}
