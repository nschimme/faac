#include <stdio.h>
#include <assert.h>
#include "../libfaac/channels.c"

void test_GetChannelInfo_Mono() {
    ChannelInfo channels[1];
    /* ISO/IEC 14496-3: Single Channel Element (SCE) configuration */
    GetChannelInfo(channels, 1, 0);
    assert(channels[0].present == 1);
    assert(channels[0].cpe == 0);
    // Note: libfaac doesn't set the .sce field explicitly in GetChannelInfo.
    // It uses .cpe = 0 and .lfe = 0 to imply SCE.
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

    assert(channels[1].present == 1);
    assert(channels[1].cpe == 1);
    assert(channels[1].ch_is_left == 0);
    assert(channels[1].paired_ch == 0);
}

int main() {
    test_GetChannelInfo_Mono();
    test_GetChannelInfo_Stereo();
    printf("test_channels passed\n");
    return 0;
}
