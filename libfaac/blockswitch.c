#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "blockswitch.h"
#include "coder.h"
#include "util.h"
#include <faac.h>

typedef float psyfloat;

#define SUBBLOCKS_PER_FRAME 8
#define ENG_WIN_PREV (0 * SUBBLOCKS_PER_FRAME)
#define ENG_WIN_CUR  (1 * SUBBLOCKS_PER_FRAME)
#define ENG_WIN_NEXT (2 * SUBBLOCKS_PER_FRAME)

typedef struct {
    psyfloat eng[3 * SUBBLOCKS_PER_FRAME];
} psydata_t;

#define PSY_TD_THRESH ((faac_real)0.5)
#define PSY_TD_HARD ((faac_real)2.0)
#define PSY_TD_HARD_MIN_SR 32000

void PsySetTdHard(PsyInfo *psyInfo, unsigned int numChannels, int tnsActive, unsigned int sampleRate) {
    faac_real hard = PSY_TD_THRESH;
    unsigned int ch;

    if (tnsActive && sampleRate >= (unsigned int)PSY_TD_HARD_MIN_SR) {
        hard = PSY_TD_HARD;
    }

    for (ch = 0; ch < numChannels; ch++) {
        psyInfo[ch].td_hard = hard;
    }
}

static void PsyCheckShort(PsyInfo * psyInfo) {
    enum { PREVS = 2, NEXTS = 2 };
    psydata_t *psydata = (psydata_t *)psyInfo->data;
    int win;
    faac_real lasteng = (faac_real)psydata->eng[ENG_WIN_CUR - PREVS];
    faac_real strength = 0.0;

    psyInfo->pending.block_type = ONLY_LONG_WINDOW;
    psyInfo->pending.use_tns = 1;

    for (win = 1; win < PREVS + SUBBLOCKS_PER_FRAME + NEXTS; win++) {
        faac_real eng = (faac_real)psydata->eng[ENG_WIN_CUR - PREVS + win];
        faac_real toteng = (eng < lasteng) ? eng : lasteng;
        faac_real volchg = FAAC_FABS(eng - lasteng);
        faac_real s = volchg / (toteng + (faac_real)1e-9);
        if (s > strength) {
            strength = s;
        }
        lasteng = eng;
    }

    if (strength > PSY_TD_THRESH && strength > psyInfo->td_hard) {
        psyInfo->pending.block_type = ONLY_SHORT_WINDOW;
        psyInfo->pending.use_tns = 0;
    }
}

static void PsyInit(GlobalPsyInfo * gpsyInfo, PsyInfo * psyInfo, unsigned int numChannels, unsigned int sampleRate) {
    unsigned int ch;
    gpsyInfo->sampleRate = (faac_real) sampleRate;

    for (ch = 0; ch < numChannels; ch++) {
        psyInfo[ch].data = AllocMemory(sizeof(psydata_t)); SetMemory(psyInfo[ch].data, 0, sizeof(psydata_t));
        psyInfo[ch].size = BLOCK_LEN_LONG;
        psyInfo[ch].sizeS = BLOCK_LEN_SHORT;
        psyInfo[ch].td_hard = PSY_TD_THRESH;
        psyInfo[ch].current.block_type = ONLY_LONG_WINDOW;
        psyInfo[ch].current.use_tns = 0;
        psyInfo[ch].pending.block_type = ONLY_LONG_WINDOW;
        psyInfo[ch].pending.use_tns = 0;
    }
}

static void PsyEnd(PsyInfo * psyInfo, unsigned int numChannels) {
    unsigned int ch;
    for (ch = 0; ch < numChannels; ch++) {
        if (psyInfo[ch].data) {
            free(psyInfo[ch].data);
            psyInfo[ch].data = NULL;
        }
    }
}

static void PsyCalculate(ChannelInfo * channelInfo, PsyInfo * psyInfo, unsigned int numChannels) {
    unsigned int ch;
    for (ch = 0; ch < numChannels; ch++) {
        if (channelInfo[ch].present) {
            if (channelInfo[ch].type == ELEMENT_LFE) {
                psyInfo[ch].pending.block_type = ONLY_LONG_WINDOW;
                psyInfo[ch].pending.use_tns = 0;
            } else {
                PsyCheckShort(&psyInfo[ch]);
            }
        }
    }
}

static void PsyBufferUpdate(GlobalPsyInfo * gpsyInfo, PsyInfo * psyInfo, faac_real * restrict p_lookahead1, faac_real * restrict p_lookahead2) {
    int win, l, n = 2 * BLOCK_LEN_SHORT;
    faac_real * restrict transBuff = gpsyInfo->sharedWorkBuffLong;
    psydata_t *psydata = (psydata_t *)psyInfo[0].data;

    /* Strategy update: current <- pending */
    psyInfo[0].current = psyInfo[0].pending;

    memmove(psydata->eng, psydata->eng + SUBBLOCKS_PER_FRAME, 2 * SUBBLOCKS_PER_FRAME * sizeof(psyfloat));

    memcpy(transBuff, p_lookahead1, BLOCK_LEN_LONG * sizeof(faac_real));
    memcpy(transBuff + BLOCK_LEN_LONG, p_lookahead2, BLOCK_LEN_LONG * sizeof(faac_real));

    for (win = 0; win < SUBBLOCKS_PER_FRAME; win++) {
        faac_real *seg = transBuff + (win * BLOCK_LEN_SHORT) + (BLOCK_LEN_LONG - BLOCK_LEN_SHORT) / 2;
        faac_real e = 0.0;
        for (l = 1; l < n; l++) {
            faac_real d = seg[l] - seg[l - 1];
            e += d * d;
        }
        psydata->eng[ENG_WIN_NEXT + win] = (psyfloat)e;
    }
}

const float *PsyGetCurEnvelope(PsyInfo *psyInfo, int *len) {
    psydata_t *psydata = (psydata_t *)psyInfo->data;
    if (len) {
        *len = SUBBLOCKS_PER_FRAME;
    }
    return (const float *)&psydata->eng[ENG_WIN_PREV];
}

static void BlockSwitch(CoderInfo * coderInfo, PsyInfo * psyInfo, unsigned int numChannels) {
    unsigned int ch;
    int desire = ONLY_LONG_WINDOW;

    if (psyInfo[0].pending.block_type == ONLY_SHORT_WINDOW) {
        desire = ONLY_SHORT_WINDOW;
    }

    for (ch = 0; ch < numChannels; ch++) {
        int lasttype = coderInfo[ch].block_type;
        int type = desire;

        if (lasttype == ONLY_LONG_WINDOW) {
            if (type == ONLY_SHORT_WINDOW) {
                type = LONG_SHORT_WINDOW;
            }
        } else if (lasttype == LONG_SHORT_WINDOW) {
            type = ONLY_SHORT_WINDOW;
        } else if (lasttype == ONLY_SHORT_WINDOW) {
            if (type == ONLY_LONG_WINDOW) {
                type = SHORT_LONG_WINDOW;
            }
        } else if (lasttype == SHORT_LONG_WINDOW) {
            type = ONLY_LONG_WINDOW;
        }

        coderInfo[ch].block_type = type;
        psyInfo[ch].current = psyInfo[ch].pending;
        psyInfo[ch].current.block_type = type;

        if (desire == ONLY_SHORT_WINDOW) {
            psyInfo[ch].current.use_tns = 0;
        }
    }
}

psymodel_t psymodel2 = { PsyInit, PsyEnd, PsyCalculate, PsyBufferUpdate, BlockSwitch };
