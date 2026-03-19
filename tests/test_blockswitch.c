#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <string.h>
#include "libfaac/coder.h"
#include "../libfaac/blockswitch.c"

void test_PsyCheckShort() {
    PsyInfo psyInfo;
    psydata_t psydata;
    memset(&psyInfo, 0, sizeof(psyInfo));
    memset(&psydata, 0, sizeof(psydata));
    psyInfo.data = &psydata;

    psydata.lastband = 10;

    float eng_quiet[NSFB_SHORT];
    float eng_loud[NSFB_SHORT];
    for(int i=0; i<NSFB_SHORT; i++) {
        eng_quiet[i] = 1.0f;
        eng_loud[i] = 100.0f;
    }

    for(int i=0; i<8; i++) {
        psydata.engPrev[i] = eng_quiet;
        psydata.eng[i] = eng_quiet;
        psydata.engNext[i] = eng_quiet;
    }

    // Stable signal
    PsyCheckShort(&psyInfo, 1.0);
    assert(psyInfo.block_type == ONLY_LONG_WINDOW);

    // Transient in engNext[0]
    psydata.engNext[0] = eng_loud;
    PsyCheckShort(&psyInfo, 1.0);
    assert(psyInfo.block_type == ONLY_SHORT_WINDOW);
}

int main() {
    test_PsyCheckShort();
    printf("test_blockswitch passed\n");
    return 0;
}
