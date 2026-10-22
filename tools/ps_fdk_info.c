#include <fdk-aac/aacenc_lib.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr, "usage: ps_fdk_info AOT BITRATE_BPS SAMPLE_RATE CHANNELS\n");
        return 2;
    }
    const int aot = atoi(argv[1]);
    const int bitrate = atoi(argv[2]);
    const int sample_rate = atoi(argv[3]);
    const int channels = atoi(argv[4]);
    if ((channels != 1 && channels != 2) || bitrate <= 0 || sample_rate <= 0) return 2;

    HANDLE_AACENCODER enc = NULL;
    if (aacEncOpen(&enc, 0, (UINT)channels) != AACENC_OK) return 3;
    const CHANNEL_MODE mode = channels == 1 ? MODE_1 : MODE_2;
    if (aacEncoder_SetParam(enc, AACENC_AOT, (UINT)aot) != AACENC_OK ||
        aacEncoder_SetParam(enc, AACENC_SAMPLERATE, (UINT)sample_rate) != AACENC_OK ||
        aacEncoder_SetParam(enc, AACENC_CHANNELMODE, (UINT)mode) != AACENC_OK ||
        aacEncoder_SetParam(enc, AACENC_BITRATE, (UINT)bitrate) != AACENC_OK ||
        aacEncoder_SetParam(enc, AACENC_TRANSMUX, TT_MP4_ADTS) != AACENC_OK ||
        aacEncoder_SetParam(enc, AACENC_AFTERBURNER, 1) != AACENC_OK) {
        aacEncClose(&enc);
        return 4;
    }
    if (aacEncEncode(enc, NULL, NULL, NULL, NULL) != AACENC_OK) {
        aacEncClose(&enc);
        return 5;
    }
    AACENC_InfoStruct info;
    if (aacEncInfo(enc, &info) != AACENC_OK) {
        aacEncClose(&enc);
        return 6;
    }
    printf("{\"frameLength\":%u,\"nDelay\":%u,\"nDelayCore\":%u,\"confSize\":%u}\n",
           info.frameLength, info.nDelay, info.nDelayCore, info.confSize);
    aacEncClose(&enc);
    return 0;
}
