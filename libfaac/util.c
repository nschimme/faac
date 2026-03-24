#include <math.h>
#include "util.h"
#include "coder.h"

int GetSRIndex(unsigned int sampleRate)
{
    if (92017 <= sampleRate) return 0;
    if (75132 <= sampleRate) return 1;
    if (55426 <= sampleRate) return 2;
    if (46009 <= sampleRate) return 3;
    if (37566 <= sampleRate) return 4;
    if (27713 <= sampleRate) return 5;
    if (23004 <= sampleRate) return 6;
    if (18783 <= sampleRate) return 7;
    if (13856 <= sampleRate) return 8;
    if (11502 <= sampleRate) return 9;
    if (9391 <= sampleRate) return 10;
    return 11;
}

unsigned int MaxBitrate(unsigned long sampleRate)
{
    return 0x2000 * 8 * (faac_real)sampleRate/(faac_real)FRAME_LEN;
}

unsigned int MinBitrate()
{
    return 8000;
}

unsigned int CalcBandwidth(unsigned long bitRate, unsigned long sampleRate)
{
    const unsigned int nyquist = sampleRate / 2;
    unsigned int bw;

    if (!bitRate) return nyquist;

    if (bitRate <= 16000) {
        /* Segment 1: Telephony (3.3kHz to 3.5kHz)
         * Narrowed to ensure undershoot at 16kbps/ch.
         */
        bw = 3300 + (bitRate * (3500 - 3300) / 16000);
    }
    else if (bitRate <= 32000) {
        /* Segment 2: Low-tier music (3.5kHz to 6kHz)
         */
        bw = 3500 + ((bitRate - 16000) * (6000 - 3500) / 16000);
    }
    else if (bitRate <= 64000) {
        /* Segment 3: Mid-tier (6kHz to 16kHz)
         */
        bw = 6000 + ((bitRate - 32000) * (16000 - 6000) / 32000);
    }
    else if (bitRate <= 128000) {
        /* Segment 4: High-fidelity (16kHz to 20kHz)
         */
        bw = 16000 + ((bitRate - 64000) * (20000 - 16000) / 64000);
    }
    else {
        bw = 20000;
    }

    return (bw > nyquist) ? nyquist : bw;
}

unsigned int BitAllocation(faac_real pe, int short_block)
{
    faac_real pew1, pew2, bit_allocation;
    if (short_block) { pew1 = 0.6; pew2 = 24.0; } else { pew1 = 0.3; pew2 = 6.0; }
    bit_allocation = pew1 * pe + pew2 * FAAC_SQRT(pe);
    bit_allocation = min(max(0.0, bit_allocation), 6144.0);
    return (unsigned int)(bit_allocation+0.5);
}

unsigned int MaxBitresSize(unsigned long bitRate, unsigned long sampleRate)
{
    return 6144 - (unsigned int)((faac_real)bitRate/(faac_real)sampleRate*(faac_real)FRAME_LEN);
}
