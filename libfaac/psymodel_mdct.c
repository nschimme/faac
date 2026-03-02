/*
 * FAAC - Freeware Advanced Audio Coder
 * MDCT-based Psychoacoustic Model implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "blockswitch.h"
#include "coder.h"
#include "fft.h"
#include "util.h"
#include <faac.h>
#include "filtbank.h"
#include "quantize.h"

#define NOISEFLOOR 0.4

typedef struct {
    faac_real sampleRate;
    faac_real ath_table[BLOCK_LEN_LONG];
} MDCTPsyGlobal;

typedef struct {
    faac_real *prevSamples;
    int block_type;
    faac_real pe;
    int pns_state[MAX_SCFAC_BANDS];
    int prev_block_type;
} MDCTPsyInfo;

static void MDCTPsyInit(GlobalPsyInfo *gpsyInfo, PsyInfo *psyInfo,
                        unsigned int numChannels, unsigned int sampleRate,
                        int *cb_width_long, int num_cb_long,
                        int *cb_width_short, int num_cb_short)
{
    unsigned int channel;
    int i;
    MDCTPsyGlobal *global = AllocMemory(sizeof(MDCTPsyGlobal));

    global->sampleRate = (faac_real)sampleRate;

    for (i = 0; i < BLOCK_LEN_LONG; i++) {
        faac_real freq = (faac_real)i * 18000.0 / BLOCK_LEN_LONG;
        faac_real fkHz = freq / 1000.0;
        faac_real ath;
        if (fkHz < 0.1) fkHz = 0.1;
        ath = 3.64 * FAAC_POW(fkHz, -0.8)
              - 6.5 * FAAC_EXP(-0.6 * FAAC_POW(fkHz - 3.3, 2.0))
              + 0.001 * FAAC_POW(fkHz, 4.0);
        global->ath_table[i] = FAAC_POW(10.0, (ath - 20.0) / 10.0);
    }

    gpsyInfo->data = global;

    for (channel = 0; channel < numChannels; channel++) {
        MDCTPsyInfo *info = AllocMemory(sizeof(MDCTPsyInfo));
        info->prevSamples = AllocMemory(BLOCK_LEN_LONG * sizeof(faac_real));
        memset(info->prevSamples, 0, BLOCK_LEN_LONG * sizeof(faac_real));
        info->block_type = ONLY_LONG_WINDOW;
        info->prev_block_type = -1;
        memset(info->pns_state, 0, sizeof(info->pns_state));
        psyInfo[channel].data = info;
    }
}

static void MDCTPsyEnd(GlobalPsyInfo *gpsyInfo, PsyInfo *psyInfo, unsigned int numChannels)
{
    unsigned int channel;
    MDCTPsyGlobal *global = (MDCTPsyGlobal *)gpsyInfo->data;

    if (global) {
        FreeMemory(global);
    }

    for (channel = 0; channel < numChannels; channel++) {
        MDCTPsyInfo *info = (MDCTPsyInfo *)psyInfo[channel].data;
        if (info) {
            if (info->prevSamples) FreeMemory(info->prevSamples);
            FreeMemory(info);
        }
    }
}

static void MDCTPsyBufferUpdate(FFT_Tables *fft_tables, GlobalPsyInfo *gpsyInfo, PsyInfo *psyInfo,
                                faac_real *newSamples, unsigned int bandwidth,
                                int *cb_width_short, int num_cb_short)
{
    MDCTPsyInfo *info = (MDCTPsyInfo *)psyInfo->data;
    /* In MDCT-based PAM, we skip the FFT entirely.
       We just store the samples for block switching detection if needed. */
    memcpy(info->prevSamples, newSamples, BLOCK_LEN_LONG * sizeof(faac_real));
}

static void MDCTPsyCalculate(ChannelInfo *channelInfo, GlobalPsyInfo *gpsyInfo,
                             PsyInfo *psyInfo, int *cb_width_long, int num_cb_long,
                             int *cb_width_short, int num_cb_short, unsigned int numChannels,
                             faac_real quality)
{
    unsigned int channel;
    for (channel = 0; channel < numChannels; channel++) {
        MDCTPsyInfo *info = (MDCTPsyInfo *)psyInfo[channel].data;
        faac_real *samples = info->prevSamples;
        int i, j;
        faac_real max_volchg = 0.0;

        /* Phase 1: Time-domain transient detection for Window Switching */
        faac_real prev_enrg = -1.0;
        for (i = 0; i < 8; i++) {
            faac_real enrg = 0.0;
            for (j = 0; j < 128; j++) {
                enrg += samples[i * 128 + j] * samples[i * 128 + j];
            }
            if (prev_enrg >= 0) {
                faac_real volchg = (enrg + 1.0) / (prev_enrg + 1.0);
                if (volchg > max_volchg) max_volchg = volchg;
            }
            prev_enrg = enrg;
        }

        /* Trigger short blocks if energy increases significantly */
        if (max_volchg > 8.0) {
            info->block_type = ONLY_SHORT_WINDOW;
        } else {
            info->block_type = ONLY_LONG_WINDOW;
        }

        /* PE will be calculated during PsyMask for higher accuracy */
    }
}

static void MDCTBlockSwitch(CoderInfo *coderInfo, PsyInfo *psyInfo, unsigned int numChannels)
{
  unsigned int channel;
  int desire = ONLY_LONG_WINDOW;

  for (channel = 0; channel < numChannels; channel++)
  {
    MDCTPsyInfo *info = (MDCTPsyInfo *)psyInfo[channel].data;
    if (info->block_type == ONLY_SHORT_WINDOW)
      desire = ONLY_SHORT_WINDOW;
  }

  for (channel = 0; channel < numChannels; channel++)
  {
    int lasttype = coderInfo[channel].block_type;

    if (desire == ONLY_SHORT_WINDOW
	|| coderInfo[channel].desired_block_type == ONLY_SHORT_WINDOW)
    {
      if (lasttype == ONLY_LONG_WINDOW || lasttype == SHORT_LONG_WINDOW)
	coderInfo[channel].block_type = LONG_SHORT_WINDOW;
      else
	coderInfo[channel].block_type = ONLY_SHORT_WINDOW;
    }
    else
    {
      if (lasttype == ONLY_SHORT_WINDOW || lasttype == LONG_SHORT_WINDOW)
	coderInfo[channel].block_type = SHORT_LONG_WINDOW;
      else
	coderInfo[channel].block_type = ONLY_LONG_WINDOW;
    }
    coderInfo[channel].desired_block_type = desire;
  }
}

static void MDCTPsyMask(GlobalPsyInfo *gpsyInfo, PsyInfo *psyInfo, CoderInfo * __restrict coderInfo, faac_real * __restrict xr0, faac_real * __restrict bandlvl,
                        faac_real * __restrict bandenrg, int gnum, faac_real quality, int spreading, int athLevel,
                        faac_real * __restrict tonality, faac_real * __restrict bandlvl_stable)
{
    int sfb, start, end, cnt;
    int *cb_offset = coderInfo->sfb_offset;
    faac_real totenrg = 0.0;
    int gsize = coderInfo->groups.len[gnum];
    const faac_real *xr;
    int win;
    int total_len = coderInfo->sfb_offset[coderInfo->sfbn];
    MDCTPsyGlobal *global = (MDCTPsyGlobal *)gpsyInfo->data;

    for (win = 0; win < gsize; win++) {
        xr = xr0 + win * BLOCK_LEN_SHORT;
        for (cnt = 0; cnt < total_len; cnt++) {
            totenrg += xr[cnt] * xr[cnt];
        }
    }

    if (totenrg < (NOISEFLOOR * NOISEFLOOR * gsize * total_len)) {
        for (sfb = 0; sfb < coderInfo->sfbn; sfb++) {
            bandlvl[sfb] = 0.0;
            bandenrg[sfb] = 0.0;
        }
        return;
    }

    for (sfb = 0; sfb < coderInfo->sfbn; sfb++) {
        faac_real avge = 0.0, maxe = 0.0;
        start = cb_offset[sfb];
        end = cb_offset[sfb + 1];

        for (win = 0; win < gsize; win++) {
            xr = xr0 + win * BLOCK_LEN_SHORT + start;
            for (cnt = 0; cnt < (end - start); cnt++) {
                faac_real val = xr[cnt];
                faac_real e = val * val;
                avge += e;
                if (maxe < e) maxe = e;
            }
        }
        bandenrg[sfb] = avge;

        if (avge > 0.0001)
            tonality[sfb] = (maxe * gsize) * (end - start) / avge;
        else
            tonality[sfb] = 0;

        faac_real target;
        faac_real powm = 0.4;
        int last = (coderInfo->block_type == ONLY_SHORT_WINDOW) ? BLOCK_LEN_SHORT : BLOCK_LEN_LONG;
        faac_real avgenrg = (totenrg / last) * (end - start);

        target = 0.2 * FAAC_POW(avge/avgenrg, powm) + 0.36 * FAAC_POW((maxe * gsize)/avgenrg, powm);
        if (coderInfo->block_type == ONLY_SHORT_WINDOW) target *= 1.5;

        target *= 10.0 / (1.0 + ((faac_real)(start+end)/last));
        bandlvl[sfb] = target * quality;
        if (bandlvl_stable) bandlvl_stable[sfb] = target;
    }

    /* Phase 4: Perceptual Entropy (PE) refinement based on SMR */
    faac_real pe = 0;
    for (sfb = 0; sfb < coderInfo->sfbn; sfb++) {
        start = cb_offset[sfb];
        end = cb_offset[sfb + 1];
        if (bandlvl[sfb] > 0.0001) {
            faac_real smr = bandenrg[sfb] / (bandlvl[sfb] / quality);
            if (smr > 1.0) {
                /* PE is roughly proportional to log of SMR */
                pe += (end - start) * FAAC_LOG10(smr);
            }
        }
    }
    /* Normalize PE to match expected range in BitAllocation (~300-3000) */
    psyInfo->pe = pe * 0.1;

    if (spreading > 0) {
        faac_real spread[MAX_SCFAC_BANDS];
        for (sfb = 0; sfb < coderInfo->sfbn; sfb++) {
            faac_real s = bandenrg[sfb];
            if (sfb > 0) s = max(s, bandenrg[sfb - 1] * 0.02 * spreading);
            if (sfb < coderInfo->sfbn - 1) s = max(s, bandenrg[sfb + 1] * 0.01 * spreading);
            spread[sfb] = s;
        }
        memcpy(bandenrg, spread, coderInfo->sfbn * sizeof(faac_real));
    }

    if (athLevel > 0) {
        faac_real sensitivity = athLevel * 0.1;
        for (sfb = 0; sfb < coderInfo->sfbn; sfb++) {
            int bcenter = (coderInfo->sfb_offset[sfb] + coderInfo->sfb_offset[sfb + 1]) >> 1;
            if (bcenter >= BLOCK_LEN_LONG) bcenter = BLOCK_LEN_LONG - 1;

            if (bandenrg[sfb] < global->ath_table[bcenter] * sensitivity)
                bandlvl[sfb] = 0.0;
        }
    }
}

psymodel_t psymodel_mdct = {
    MDCTPsyInit,
    MDCTPsyEnd,
    MDCTPsyCalculate,
    MDCTPsyBufferUpdate,
    MDCTBlockSwitch,
    MDCTPsyMask
};
