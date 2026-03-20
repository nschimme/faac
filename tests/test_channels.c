/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2026 Nils Schimmelmann
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <assert.h>
#include "../libfaac/channels.c"

void test_GetChannelInfo_Mono() {
    ChannelInfo channels[1];
    /* ISO/IEC 14496-3: Single Channel Element (SCE) configuration */
    GetChannelInfo(channels, 1, 0);
    assert(channels[0].present == 1);
    assert(channels[0].type == ELEMENT_SCE);
}

void test_GetChannelInfo_Stereo() {
    ChannelInfo channels[2];
    /* ISO/IEC 14496-3: Channel Pair Element (CPE) configuration */
    GetChannelInfo(channels, 2, 0);
    assert(channels[0].present == 1);
    assert(channels[0].type == ELEMENT_CPE);
    assert(channels[0].ch_is_left == 1);
    assert(channels[0].paired_ch == 1);

    assert(channels[1].present == 1);
    assert(channels[1].type == ELEMENT_CPE);
    assert(channels[1].ch_is_left == 0);
    assert(channels[1].paired_ch == 0);
}

void test_GetChannelInfo_3_0() {
    /* 3.0 (3 ch): 1 SCE + 1 CPE
     * Standard order: SCE (Center), CPE (Front L/R).
     * NOTE: FAAC generates the element sequence SCE -> CPE.
     * This mapping aligns with MPEG standards for 3-channel layouts.
     */
    ChannelInfo channels[3];
    GetChannelInfo(channels, 3, 0);

    /* SCE (Center) */
    assert(channels[0].present == 1);
    assert(channels[0].type == ELEMENT_SCE);
    assert(channels[0].tag == 0);

    /* CPE (Left/Right) */
    assert(channels[1].present == 1);
    assert(channels[1].type == ELEMENT_CPE);
    assert(channels[1].ch_is_left == 1);
    assert(channels[1].paired_ch == 2);
    assert(channels[1].tag == 0);

    assert(channels[2].present == 1);
    assert(channels[2].type == ELEMENT_CPE);
    assert(channels[2].ch_is_left == 0);
    assert(channels[2].paired_ch == 1);
}

void test_GetChannelInfo_5_1() {
    /* 5.1 (6 ch): 1 SCE + 2 CPE + 1 LFE
     * Standard order (Config 6): SCE (Center), CPE (Front L/R), CPE (Surround L/R), LFE (LFE).
     * NOTE: FAAC generates the element sequence SCE -> 2xCPE -> LFE.
     * This mapping aligns with the ISO/IEC 14496-3 definition for 6-channel layouts.
     */
    ChannelInfo channels[6];
    GetChannelInfo(channels, 6, 1);

    /* SCE (Center) */
    assert(channels[0].present == 1);
    assert(channels[0].type == ELEMENT_SCE);
    assert(channels[0].tag == 0);

    /* CPE 1 (Front L/R) */
    assert(channels[1].present == 1);
    assert(channels[1].type == ELEMENT_CPE);
    assert(channels[1].ch_is_left == 1);
    assert(channels[1].paired_ch == 2);
    assert(channels[1].tag == 0);

    assert(channels[2].present == 1);
    assert(channels[2].type == ELEMENT_CPE);
    assert(channels[2].ch_is_left == 0);
    assert(channels[2].paired_ch == 1);

    /* CPE 2 (Surround L/R) */
    assert(channels[3].present == 1);
    assert(channels[3].type == ELEMENT_CPE);
    assert(channels[3].ch_is_left == 1);
    assert(channels[3].paired_ch == 4);
    assert(channels[3].tag == 1);

    assert(channels[4].present == 1);
    assert(channels[4].type == ELEMENT_CPE);
    assert(channels[4].ch_is_left == 0);
    assert(channels[4].paired_ch == 3);

    /* LFE */
    assert(channels[5].present == 1);
    assert(channels[5].type == ELEMENT_LFE);
    assert(channels[5].tag == 0);
}

void test_GetChannelInfo_7_1() {
    /* 7.1 (8 ch): 1 SCE + 3 CPE + 1 LFE
     * Standard order (Config 7): SCE (Center), CPE (Front L/R), CPE (Side L/R), CPE (Back L/R), LFE (LFE).
     * NOTE: FAAC generates the element sequence SCE -> 3xCPE -> LFE.
     * LIMITATION: While the element layout is correct for 8 channels, the ADTS header
     * 3-bit channel_configuration field (ISO/IEC 13818-7) will overflow/wrap to 0
     * for numChannels=8, leading to non-standard bitstreams unless PCE is used.
     */
    ChannelInfo channels[8];
    GetChannelInfo(channels, 8, 1);

    /* SCE (Center) */
    assert(channels[0].present == 1);
    assert(channels[0].type == ELEMENT_SCE);

    /* CPE 1 (Front L/R) */
    assert(channels[1].present == 1);
    assert(channels[1].type == ELEMENT_CPE);
    assert(channels[1].ch_is_left == 1);
    assert(channels[1].paired_ch == 2);
    assert(channels[1].tag == 0);

    assert(channels[2].present == 1);
    assert(channels[2].type == ELEMENT_CPE);
    assert(channels[2].ch_is_left == 0);
    assert(channels[2].paired_ch == 1);

    /* CPE 2 (Side L/R) */
    assert(channels[3].present == 1);
    assert(channels[3].type == ELEMENT_CPE);
    assert(channels[3].ch_is_left == 1);
    assert(channels[3].paired_ch == 4);
    assert(channels[3].tag == 1);

    assert(channels[4].present == 1);
    assert(channels[4].type == ELEMENT_CPE);
    assert(channels[4].ch_is_left == 0);
    assert(channels[4].paired_ch == 3);

    /* CPE 3 (Back L/R) */
    assert(channels[5].present == 1);
    assert(channels[5].type == ELEMENT_CPE);
    assert(channels[5].ch_is_left == 1);
    assert(channels[5].paired_ch == 6);
    assert(channels[5].tag == 2);

    assert(channels[6].present == 1);
    assert(channels[6].type == ELEMENT_CPE);
    assert(channels[6].ch_is_left == 0);
    assert(channels[6].paired_ch == 5);

    /* LFE */
    assert(channels[7].present == 1);
    assert(channels[7].type == ELEMENT_LFE);
}

int main() {
    test_GetChannelInfo_Mono();
    test_GetChannelInfo_Stereo();
    test_GetChannelInfo_3_0();
    test_GetChannelInfo_5_1();
    test_GetChannelInfo_7_1();
    printf("test_channels passed\n");
    return 0;
}
