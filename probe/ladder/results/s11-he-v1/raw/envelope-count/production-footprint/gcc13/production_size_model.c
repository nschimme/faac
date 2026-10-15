#include <stdio.h>
#ifndef MAX_CHANNELS
#define MAX_CHANNELS 8
#endif
#include "sbr_internal.h"

int main(void)
{
    printf("SBR_MAX_ENVELOPES=%d\n", SBR_MAX_ENVELOPES);
    printf("SBR_FRAME_FIFO=%d\n", SBR_FRAME_FIFO);
    printf("sizeof_SbrEnvData=%zu\n", sizeof(SbrEnvData));
    printf("sizeof_SbrFrameData=%zu\n", sizeof(SbrFrameData));
    printf("sizeof_SBRChannel=%zu\n", sizeof(SBRChannel));
    printf("sizeof_SBRInfo=%zu\n", sizeof(SBRInfo));
    printf("sizeof_SBRContext=%zu\n", sizeof(SBRContext));
    printf("sizeof_SignalAnalysisChannel=%zu\n", sizeof(SignalAnalysisChannel));
    printf("sizeof_SignalAnalysis=%zu\n", sizeof(SignalAnalysis));
    for (int channels = 1; channels <= 8; channels *= 2) {
        size_t sbr_info_alloc = sizeof(SBRInfo) + (size_t)channels * sizeof(SBRChannel);
        size_t frame_size = sizeof(SbrFrameData) + (size_t)channels * sizeof(SbrEnvData);
        size_t sbr_context_alloc = sizeof(SBRContext) + (size_t)SBR_FRAME_FIFO * frame_size;
        size_t analysis_alloc = sizeof(SignalAnalysis) + (size_t)channels * sizeof(SignalAnalysisChannel);
        printf("channels_%d_sbr_info_alloc=%zu\n", channels, sbr_info_alloc);
        printf("channels_%d_sbr_context_alloc=%zu\n", channels, sbr_context_alloc);
        printf("channels_%d_signal_analysis_alloc=%zu\n", channels, analysis_alloc);
        printf("channels_%d_total_excluding_resampler=%zu\n", channels,
               sbr_info_alloc + sbr_context_alloc + analysis_alloc);
    }
    return 0;
}
