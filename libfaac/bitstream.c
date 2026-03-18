/**********************************************************************
FAAC Bitstream Module
**********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "coder.h"
#include "channels.h"
#include "huff2.h"
#include "bitstream.h"
#include "util.h"

static int WriteADTSHeader(faacEncStruct* hEncoder, BitStream *bitStream, int writeFlag)
{
    if (writeFlag) {
        PutBit(bitStream, 0xFFFF, 12);
        PutBit(bitStream, hEncoder->config.mpegVersion, 1);
        PutBit(bitStream, 0, 2);
        PutBit(bitStream, 1, 1);
        PutBit(bitStream, hEncoder->config.aacObjectType - 1, 2);
        PutBit(bitStream, hEncoder->sampleRateIdx, 4);
        PutBit(bitStream, 0, 1);
        PutBit(bitStream, hEncoder->numChannels, 3);
        PutBit(bitStream, 0, 1);
        PutBit(bitStream, 0, 1);
        PutBit(bitStream, 0, 1);
        PutBit(bitStream, 0, 1);
        PutBit(bitStream, hEncoder->usedBytes, 13);
        PutBit(bitStream, 0x7FF, 11);
        PutBit(bitStream, 0, 2);
    }
    return 56;
}

static int WriteFAACStr(BitStream *bitStream, char *version, int write)
{
    int i, padbits, count, bitcnt;
    char str[200];
    sprintf(str, "libfaac %s", version);
    int len = strlen(str) + 1;
    padbits = (8 - ((bitStream->numBit + 7) % 8)) % 8;
    count = len + 3;
    bitcnt = LEN_SE_ID + 4 + ((count < 15) ? 0 : 8) + count * 8;
    if (!write) return bitcnt;
    PutBit(bitStream, ID_FIL, LEN_SE_ID);
    if (count < 15) PutBit(bitStream, count, 4);
    else { PutBit(bitStream, 15, 4); PutBit(bitStream, count - 14, 8); }
    PutBit(bitStream, 0, padbits);
    PutBit(bitStream, 0, 8); PutBit(bitStream, 0, 8);
    for (i = 0; i < len; i++) PutBit(bitStream, str[i], 8);
    PutBit(bitStream, 0, 8 - padbits);
    return bitcnt;
}

static int WriteSpectralData(CoderInfo *coderInfo, BitStream *bitStream, int writeFlag)
{
    int i, bits = 0;
    for(i = 0; i < coderInfo->datacnt; i++) {
        if (writeFlag && coderInfo->s[i].len > 0)
            PutBit(bitStream, coderInfo->s[i].data, coderInfo->s[i].len);
        bits += coderInfo->s[i].len;
    }
    return bits;
}

static int WriteTNSData(CoderInfo *coderInfo, BitStream *bitStream, int writeFlag)
{
    int bits = 0;
    TnsInfo* tnsInfoPtr = &coderInfo->tnsInfo;
    if (writeFlag) PutBit(bitStream, tnsInfoPtr->tnsDataPresent, LEN_TNS_PRES);
    bits += LEN_TNS_PRES;
    if (!tnsInfoPtr->tnsDataPresent) return bits;
    int nw = (coderInfo->block_type == ONLY_SHORT_WINDOW) ? MAX_SHORT_WINDOWS : 1;
    int ln = (coderInfo->block_type == ONLY_SHORT_WINDOW) ? LEN_TNS_NFILTS : LEN_TNS_NFILTL;
    int ll = (coderInfo->block_type == ONLY_SHORT_WINDOW) ? LEN_TNS_LENGTHS : LEN_TNS_LENGTHL;
    int lo = (coderInfo->block_type == ONLY_SHORT_WINDOW) ? LEN_TNS_ORDERS : LEN_TNS_ORDERL;
    int w, f, i;
    for (w=0; w<nw; w++) {
        TnsWindowData* wd = &tnsInfoPtr->windowData[w];
        if (writeFlag) PutBit(bitStream, wd->numFilters, ln);
        bits += ln;
        if (wd->numFilters) {
            if (writeFlag) PutBit(bitStream, wd->coefResolution - DEF_TNS_RES_OFFSET, LEN_TNS_COEFF_RES);
            bits += LEN_TNS_COEFF_RES;
            for (f=0; f<wd->numFilters; f++) {
                TnsFilterData* fd = &wd->tnsFilter[f];
                if (writeFlag) { PutBit(bitStream, fd->length, ll); PutBit(bitStream, fd->order, lo); }
                bits += ll + lo;
                if (fd->order) {
                    if (writeFlag) { PutBit(bitStream, fd->direction, LEN_TNS_DIRECTION); PutBit(bitStream, fd->coefCompress, LEN_TNS_COMPRESS); }
                    bits += LEN_TNS_DIRECTION + LEN_TNS_COMPRESS;
                    int res = wd->coefResolution - fd->coefCompress;
                    bits += fd->order * res;
                    if (writeFlag) {
                        for (i=1; i<=fd->order; i++)
                            PutBit(bitStream, (unsigned long)fd->index[i] & (~0UL >> (32-res)), res);
                    }
                }
            }
        }
    }
    return bits;
}

static int WriteAACFillBits(BitStream* bitStream, int numBits, int writeFlag)
{
    int left = numBits;
    while (left >= (LEN_SE_ID + LEN_F_CNT)) {
        if (writeFlag) PutBit(bitStream, ID_FIL, LEN_SE_ID);
        left -= LEN_SE_ID;
        int nb = left / 8;
        int maxc = (1 << LEN_F_CNT) - 1;
        if (nb < maxc) {
            if (writeFlag) {
                PutBit(bitStream, nb, LEN_F_CNT);
                int i; for (i=0; i<nb; i++) PutBit(bitStream, 0, 8);
            }
            left -= (LEN_F_CNT + nb * 8);
        } else {
            int maxe = (1 << 8) - 1;
            int total = nb;
            if (total > (maxc + maxe)) total = maxc + maxe;
            int esc = total - maxc;
            if (writeFlag) {
                PutBit(bitStream, maxc, LEN_F_CNT); PutBit(bitStream, esc, 8);
                int i; for (i=0; i < total-1; i++) PutBit(bitStream, 0, 8);
            }
            left -= (LEN_F_CNT + 8 + (total-1) * 8);
        }
    }
    return left;
}

static int FindGroupingBits(CoderInfo *coderInfo)
{
    int grouping_bits = 0, tmp[8], i, j, index = 0;
    for(i = 0; i < coderInfo->groups.n; i++)
        for (j = 0; j < coderInfo->groups.len[i]; j++)
            tmp[index++] = i;
    for(i = 1; i < 8; i++) {
        grouping_bits <<= 1;
        if(tmp[i] == tmp[i-1]) grouping_bits++;
    }
    return grouping_bits;
}

static int WriteICSInfo(CoderInfo *coderInfo, BitStream *bitStream, int writeFlag)
{
    int bits = LEN_ICS_RESERV + LEN_WIN_SEQ + LEN_WIN_SH;
    if (writeFlag) {
        PutBit(bitStream, 0, LEN_ICS_RESERV);
        PutBit(bitStream, coderInfo->block_type, LEN_WIN_SEQ);
        PutBit(bitStream, coderInfo->window_shape, LEN_WIN_SH);
    }
    if (coderInfo->block_type == ONLY_SHORT_WINDOW) {
        if (writeFlag) {
            PutBit(bitStream, coderInfo->sfbn, LEN_MAX_SFBS);
            PutBit(bitStream, FindGroupingBits(coderInfo), MAX_SHORT_WINDOWS - 1);
        }
        bits += LEN_MAX_SFBS + MAX_SHORT_WINDOWS - 1;
    } else {
        if (writeFlag) {
            PutBit(bitStream, coderInfo->sfbn, LEN_MAX_SFBL);
            PutBit(bitStream, 0, LEN_PRED_PRES);
        }
        bits += LEN_MAX_SFBL + LEN_PRED_PRES;
    }
    return bits;
}

static int WriteICS(CoderInfo *coderInfo, BitStream *bitStream, int commonWindow, int writeFlag)
{
    int bits = LEN_GLOB_GAIN;
    if (writeFlag) PutBit(bitStream, coderInfo->global_gain, LEN_GLOB_GAIN);
    if (!commonWindow) bits += WriteICSInfo(coderInfo, bitStream, writeFlag);
    bits += writebooks(coderInfo, bitStream, writeFlag);
    bits += writesf(coderInfo, bitStream, writeFlag);
    if (writeFlag) PutBit(bitStream, 0, LEN_PULSE_PRES);
    bits += LEN_PULSE_PRES + WriteTNSData(coderInfo, bitStream, writeFlag);
    if (writeFlag) PutBit(bitStream, 0, LEN_GAIN_PRES);
    bits += LEN_GAIN_PRES + WriteSpectralData(coderInfo, bitStream, writeFlag);
    return bits;
}

static int WriteSCE(CoderInfo *coderInfo, ChannelInfo *channelInfo, BitStream *bitStream, int writeFlag)
{
    if (writeFlag) {
        PutBit(bitStream, ID_SCE, LEN_SE_ID);
        PutBit(bitStream, channelInfo->tag, LEN_TAG);
    }
    return LEN_SE_ID + LEN_TAG + WriteICS(coderInfo, bitStream, 0, writeFlag);
}

static int WriteCPE(CoderInfo *coderInfoL, CoderInfo *coderInfoR, ChannelInfo *channelInfo, BitStream* bitStream, int writeFlag)
{
    int bits = LEN_SE_ID + LEN_TAG + LEN_COM_WIN;
    if (writeFlag) {
        PutBit(bitStream, ID_CPE, LEN_SE_ID);
        PutBit(bitStream, channelInfo->tag, LEN_TAG);
        PutBit(bitStream, channelInfo->common_window, LEN_COM_WIN);
    }
    if (channelInfo->common_window) {
        bits += WriteICSInfo(coderInfoL, bitStream, writeFlag);
        bits += LEN_MASK_PRES;
        if (writeFlag) PutBit(bitStream, channelInfo->msInfo.is_present, LEN_MASK_PRES);
        if (channelInfo->msInfo.is_present) {
            int i;
            for (i=0; i < coderInfoL->groups.n * coderInfoL->sfbn; i++) {
                if (writeFlag) PutBit(bitStream, channelInfo->msInfo.ms_used[i], LEN_MASK);
                bits += LEN_MASK;
            }
        }
    }
    bits += WriteICS(coderInfoL, bitStream, channelInfo->common_window, writeFlag);
    bits += WriteICS(coderInfoR, bitStream, channelInfo->common_window, writeFlag);
    return bits;
}

static int WriteLFE(CoderInfo *coderInfo, ChannelInfo *channelInfo, BitStream *bitStream, int writeFlag)
{
    if (writeFlag) {
        PutBit(bitStream, ID_LFE, LEN_SE_ID);
        PutBit(bitStream, channelInfo->tag, LEN_TAG);
    }
    return LEN_SE_ID + LEN_TAG + WriteICS(coderInfo, bitStream, 0, writeFlag);
}

static int ByteAlign(BitStream *bitStream, int writeFlag, int bitsSoFar)
{
    int len = writeFlag ? bitStream->numBit : bitsSoFar;
    int j = (8 - (len % 8)) % 8;
    if (writeFlag && j > 0) PutBit(bitStream, 0, j);
    return j;
}

static int InternalWriteBitstream(faacEncStruct* hEncoder, CoderInfo *coderInfo, ChannelInfo *channelInfo, BitStream *bitStream, int numChannel, int writeFlag)
{
    int channel, bits = 0;
    if (hEncoder->config.outputFormat == 1) bits += WriteADTSHeader(hEncoder, bitStream, writeFlag);
    if (hEncoder->frameNum == 4) bits += WriteFAACStr(bitStream, hEncoder->config.name, writeFlag);
    for (channel = 0; channel < numChannel; channel++) {
        if (channelInfo[channel].present) {
            if (!channelInfo[channel].cpe) {
                if (channelInfo[channel].lfe) bits += WriteLFE(&coderInfo[channel], &channelInfo[channel], bitStream, writeFlag);
                else bits += WriteSCE(&coderInfo[channel], &channelInfo[channel], bitStream, writeFlag);
            } else if (channelInfo[channel].ch_is_left) {
                bits += WriteCPE(&coderInfo[channel], &coderInfo[channelInfo[channel].paired_ch], &channelInfo[channel], bitStream, writeFlag);
            }
        }
    }
    if (!writeFlag) hEncoder->audioBits = bits;
    if (!writeFlag) {
        hEncoder->paddingBits = 0;
        if (hEncoder->config.bitRate && hEncoder->reservoir_max > 0) {
            int target = calculate_target_bits(hEncoder->config.bitRate, hEncoder->numChannels, hEncoder->sampleRate);
            int est = bits; if (est < 5) est = 5; est += 6 + 3 + 4;
            int surplus = target - est;
            if (surplus >= 7) hEncoder->paddingBits = (unsigned int)surplus;
            else {
                hEncoder->reservoir_bits += surplus;
                if (hEncoder->reservoir_bits > hEncoder->reservoir_max) hEncoder->reservoir_bits = hEncoder->reservoir_max;
                if (hEncoder->reservoir_bits < -hEncoder->reservoir_max) hEncoder->reservoir_bits = -hEncoder->reservoir_max;
            }
        }
    }
    int fill = 6 + hEncoder->paddingBits; bits += (fill - WriteAACFillBits(bitStream, fill, writeFlag));
    bits += LEN_SE_ID; if (writeFlag) PutBit(bitStream, ID_END, LEN_SE_ID);
    bits += ByteAlign(bitStream, writeFlag, bits);
    return bits;
}

int WriteBitstream(faacEncStruct* hEncoder, CoderInfo *coderInfo, ChannelInfo *channelInfo, BitStream *bitStream, int numChannel)
{
    int bits = InternalWriteBitstream(hEncoder, coderInfo, channelInfo, bitStream, numChannel, 0);
    if (bits < 0) return -1;
    hEncoder->usedBytes = (bits + 7) / 8;
    bitStream->numBit = bitStream->currentBit = 0;
    if (bitStream->data) memset(bitStream->data, 0, bitStream->size);
    return InternalWriteBitstream(hEncoder, coderInfo, channelInfo, bitStream, numChannel, 1);
}

BitStream *OpenBitStream(int size, unsigned char *buffer)
{
    BitStream *bitStream = malloc(sizeof(BitStream)); bitStream->size = size; bitStream->numBit = bitStream->currentBit = 0; bitStream->data = buffer; if (bitStream->data) memset(bitStream->data, 0, size); return bitStream;
}

int CloseBitStream(BitStream *bitStream) { int bytes = (bitStream->numBit + 7) / 8; free(bitStream); return bytes; }

int PutBit(BitStream *bitStream, unsigned long data, int numBit)
{
    if (numBit <= 0) return 0;
    unsigned int cur = (unsigned int)bitStream->currentBit;
    bitStream->currentBit += numBit; bitStream->numBit = bitStream->currentBit;
    if (bitStream->data) {
        unsigned int off = cur & 7; unsigned char *p = bitStream->data + (cur >> 3); data &= (1UL << numBit) - 1;
        if (off + numBit <= 8) { if (off == 0) *p = 0; *p |= (unsigned char)(data << (8 - off - numBit)); }
        else {
            int first = 8 - off; if (off == 0) *p = 0; *p++ |= (unsigned char)(data >> (numBit - first)); numBit -= first;
            while (numBit >= 8) { *p++ = (unsigned char)((data >> (numBit - 8)) & 0xFF); numBit -= 8; }
            if (numBit > 0) *p = (unsigned char)((data & ((1UL << numBit) - 1)) << (8 - numBit));
        }
    }
    return 0;
}
