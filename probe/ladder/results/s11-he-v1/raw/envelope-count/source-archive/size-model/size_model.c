#include <stdio.h>
#include "sbr_internal.h"

int main(void)
{
    printf("SbrEnvData=%zu\n", sizeof(SbrEnvData));
    printf("SbrFrameData=%zu\n", sizeof(SbrFrameData));
    printf("SBRChannel=%zu\n", sizeof(SBRChannel));
    printf("SBRInfo=%zu\n", sizeof(SBRInfo));
    printf("SBRContext=%zu\n", sizeof(SBRContext));
    printf("SignalAnalysisChannel=%zu\n", sizeof(SignalAnalysisChannel));
    printf("SignalAnalysis=%zu\n", sizeof(SignalAnalysis));
#ifdef SBR_MAX_ENVELOPES
    printf("SBR_MAX_ENVELOPES=%d\n", SBR_MAX_ENVELOPES);
#endif
    return 0;
}
