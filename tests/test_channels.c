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
    assert(channels[0].cpe == 0);
    assert(channels[0].sce == 1);
    assert(channels[0].lfe == 0);
}

void test_GetChannelInfo_Stereo() {
    ChannelInfo channels[2];
    /* ISO/IEC 14496-3: Channel Pair Element (CPE) configuration */
    GetChannelInfo(channels, 2, 0);
    assert(channels[0].present == 1);
    assert(channels[0].cpe == 1);
    assert(channels[0].ch_is_left == 1);
    assert(channels[0].paired_ch == 1);
    assert(channels[0].sce == 0);

    assert(channels[1].present == 1);
    assert(channels[1].cpe == 1);
    assert(channels[1].ch_is_left == 0);
    assert(channels[1].paired_ch == 0);
    assert(channels[1].sce == 0);
}

int main() {
    test_GetChannelInfo_Mono();
    test_GetChannelInfo_Stereo();
    printf("test_channels passed\n");
    return 0;
}
