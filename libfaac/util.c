#include <math.h>
#include "util.h"
#include "coder.h"
int GetSRIndex(unsign
    const unsigned int nyquist = sampleRate / 2;
    unsigned int bw;
    if (!bitRate) return nyquist;
    if (bitRate <= 32000) {
        bw = 4000 + (bitRate * 6000 / 32000); // 32k -> 10000
    } else if (bitRate <= 64000) {
        bw = 10000 + ((bitRate - 32000) * 9000 / 32000); // 64k -> 19000
    } else {
        bw = 19000 + ((bitRate - 64000) / 16);
        if (bw > 20000) bw = 20000;
    }

