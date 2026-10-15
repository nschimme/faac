#include <stdio.h>
#include <stddef.h>
#include "sbr_internal.h"

int main(void)
{
    const size_t sizes[] = {
        sizeof(SBRInfo), sizeof(SBRChannel), sizeof(SbrFrameData),
        sizeof(SbrEnvData), sizeof(SBRContext), sizeof(SignalAnalysis),
        sizeof(SignalAnalysisChannel), SBR_FRAME_FIFO
    };
    printf("{\"sizeof\":{\"SBRInfo\":%zu,\"SBRChannel\":%zu,\"SbrFrameData\":%zu,\"SbrEnvData\":%zu,\"SBRContext\":%zu,\"SignalAnalysis\":%zu,\"SignalAnalysisChannel\":%zu,\"SBR_FRAME_FIFO\":%zu},\"allocations\":{",
           sizes[0], sizes[1], sizes[2], sizes[3], sizes[4], sizes[5], sizes[6], sizes[7]);
    for (int i = 0; i < 3; i++) {
        int channels = i == 0 ? 1 : i == 1 ? 2 : 8;
        size_t sbr = sizeof(SBRInfo) + channels * sizeof(SBRChannel);
        size_t context = sizeof(SBRContext) + SBR_FRAME_FIFO * (sizeof(SbrFrameData) + channels * sizeof(SbrEnvData));
        size_t analysis = sizeof(SignalAnalysis) + channels * sizeof(SignalAnalysisChannel);
        size_t total = sbr + context + analysis;
        printf("%s\"%d\":{\"SBRInfo_allocation\":%zu,\"SBRContext_allocation\":%zu,\"SignalAnalysis_allocation\":%zu,\"total_bytes\":%zu}",
               i ? "," : "", channels, sbr, context, analysis, total);
    }
    puts("}}");
    return 0;
}
