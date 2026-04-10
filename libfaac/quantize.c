/****************************************************************************
    Quantizer core functions
****************************************************************************/

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "quantize.h"
#include "huff2.h"
#include "cpu_compute.h"
#include "util.h"

#ifdef __GNUC__
#define GCC_VERSION (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)
#endif

typedef void (*QuantizeFunc)(const faac_real * __restrict xr, int * __restrict xi, int n, faac_real sfacfix);

#if defined(HAVE_SSE2)
extern void quantize_sse2(const faac_real * __restrict xr, int * __restrict xi, int n, faac_real sfacfix);
#endif

static void quantize_scalar(const faac_real * __restrict xr, int * __restrict xi, int n, const faac_real sfacfix)
{
    const faac_real magic = MAGIC_NUMBER;
    const faac_real max_val = (faac_real)MAX_HUFF_ESC_VAL;
    int cnt;
    for (cnt = 0; cnt < n; cnt++)
    {
        faac_real tmp = FAAC_FABS(xr[cnt]);
        tmp *= sfacfix;
        tmp = FAAC_SQRT(tmp * FAAC_SQRT(tmp));
        tmp += magic;
        if (UNLIKELY(tmp > max_val)) tmp = max_val;
        int q = (int)tmp;
        xi[cnt] = (xr[cnt] < 0) ? -q : q;
    }
}

static QuantizeFunc qfunc = quantize_scalar;

void QuantizeInit(void)
{
#if defined(HAVE_SSE2)
    CPUCaps caps = get_cpu_caps();
    if (caps & CPU_CAP_SSE2)
        qfunc = quantize_sse2;
    else
#endif
        qfunc = quantize_scalar;
}

#define NOISEFLOOR 0.4

static void bmask(CoderInfo * __restrict coderInfo, faac_real * __restrict xr0, faac_real * __restrict bandqual,
                  faac_real * __restrict bandenrg, int gnum, faac_real quality)
{
    int sfb, start, end, cnt, win;
    int *cb_offset = coderInfo->sfb_offset;
    faac_real powm = 0.4, totenrg = 0.0;
    int gsize = coderInfo->groups.len[gnum];
    int total_len = coderInfo->sfb_offset[coderInfo->sfbn];

    for (win = 0; win < gsize; win++) {
        const faac_real *xr = xr0 + win * BLOCK_LEN_SHORT;
        for (cnt = 0; cnt < total_len; cnt++) totenrg += xr[cnt] * xr[cnt];
    }

    if (totenrg < ((NOISEFLOOR * NOISEFLOOR) * (faac_real)(gsize * total_len))) {
        for (sfb = 0; sfb < coderInfo->sfbn; sfb++) {
            bandqual[sfb] = 0.0; bandenrg[sfb] = 0.0;
        }
        return;
    }

    for (sfb = 0; sfb < coderInfo->sfbn; sfb++) {
        faac_real avge = 0.0, maxe = 0.0, target, avgenrg;
        int last;
        start = cb_offset[sfb]; end = cb_offset[sfb + 1];
        for (win = 0; win < gsize; win++) {
            const faac_real *xr = xr0 + win * BLOCK_LEN_SHORT + start;
            int n = end - start;
            for (cnt = 0; cnt < n; cnt++) {
                faac_real val = xr[cnt], e = val * val;
                avge += e; if (maxe < e) maxe = e;
            }
        }
        bandenrg[sfb] = avge; maxe *= gsize;
        if (coderInfo->block_type == ONLY_SHORT_WINDOW) {
            last = BLOCK_LEN_SHORT; avgenrg = (totenrg / last) * (end - start);
            target = 0.2 * FAAC_POW(avge/avgenrg, powm) + 0.8 * 0.45 * FAAC_POW(maxe/avgenrg, powm);
            target *= 1.5;
        } else {
            last = BLOCK_LEN_LONG; avgenrg = (totenrg / last) * (end - start);
            target = 0.2 * FAAC_POW(avge/avgenrg, powm) + 0.8 * 0.45 * FAAC_POW(maxe/avgenrg, powm);
        }
        bandqual[sfb] = target * quality * (10.0 / (1.0 + ((faac_real)(start+end)/last)));
    }
}

static void qlevel(CoderInfo * __restrict coderInfo, const faac_real * __restrict xr0, const faac_real * __restrict bandqual,
                   const faac_real * __restrict bandenrg, int gnum, int pnslevel)
{
    int sb, gsize = coderInfo->groups.len[gnum];
#if !defined(__clang__) && defined(__GNUC__) && (GCC_VERSION >= 40600)
    static const faac_real sfstep = 1.0 / FAAC_LOG10(FAAC_SQRT(FAAC_SQRT(2.0)));
#else
    static const faac_real sfstep = 20 / 1.50515;
#endif
    faac_real pnsthr = 0.1 * pnslevel;
    for (sb = 0; sb < coderInfo->sfbn; sb++) {
        faac_real sfacfix, rmsx, etot;
        int sfac, xitab[8 * 36], *xi, start, end, win;
        if (coderInfo->book[coderInfo->bandcnt] != HCB_NONE) {
            coderInfo->bandcnt++;
            continue;
        }
        start = coderInfo->sfb_offset[sb]; end = coderInfo->sfb_offset[sb+1];
        etot = bandenrg[sb] / (faac_real)gsize; rmsx = FAAC_SQRT(etot / (end - start));
        if ((rmsx < NOISEFLOOR) || (!bandqual[sb])) {
            coderInfo->book[coderInfo->bandcnt++] = HCB_ZERO;
            continue;
        }
        if (bandqual[sb] < pnsthr) {
            coderInfo->book[coderInfo->bandcnt] = HCB_PNS;
            coderInfo->sf[coderInfo->bandcnt] += FAAC_LRINT(FAAC_LOG10(etot) * (0.5 * sfstep));
            coderInfo->bandcnt++;
            continue;
        }
        sfac = FAAC_LRINT(FAAC_LOG10(bandqual[sb] / rmsx) * sfstep);
        if ((SF_OFFSET - sfac) < SF_MIN) sfacfix = 0.0;
        else sfacfix = FAAC_POW(10, sfac / sfstep);
        end -= start; xi = xitab;
        if (sfacfix <= 0.0) {
            memset(xi, 0, gsize * end * sizeof(int));
        } else {
            for (win = 0; win < gsize; win++) {
                qfunc(xr0 + win * BLOCK_LEN_SHORT + start, xi, end, sfacfix);
                xi += end;
            }
        }
        huffbook(coderInfo, xitab, gsize * end);
        coderInfo->sf[coderInfo->bandcnt++] += SF_OFFSET - sfac;
    }
}

int BlocQuant(CoderInfo * __restrict coder, faac_real * __restrict xr, AACQuantCfg *aacquantCfg)
{
    faac_real bandlvl[MAX_SCFAC_BANDS], bandenrg[MAX_SCFAC_BANDS], *gxr = xr;
    int cnt, lastis = 0, lastsf = 0;
    coder->bandcnt = 0; coder->datacnt = 0;
    for (cnt = 0; cnt < coder->groups.n; cnt++) {
        bmask(coder, gxr, bandlvl, bandenrg, cnt, (faac_real)aacquantCfg->quality/DEFQUAL);
        qlevel(coder, gxr, bandlvl, bandenrg, cnt, aacquantCfg->pnslevel);
        gxr += coder->groups.len[cnt] * BLOCK_LEN_SHORT;
    }
    for (cnt = 0; cnt < coder->bandcnt; cnt++) {
        int book = coder->book[cnt];
        if (book != HCB_ZERO && book != HCB_NONE && book != HCB_INTENSITY && book != HCB_INTENSITY2) {
            coder->global_gain = coder->sf[cnt];
            break;
        }
    }
    lastsf = coder->global_gain;
    for (cnt = 0; cnt < coder->bandcnt; cnt++) {
        int book = coder->book[cnt];
        if (book == HCB_INTENSITY || book == HCB_INTENSITY2) {
            int diff = ClampSfDiff(coder->sf[cnt] - lastis);
            lastis += diff;
            coder->sf[cnt] = lastis;
        } else if (book != HCB_ZERO && book != HCB_NONE && book != HCB_PNS) {
            int diff = ClampSfDiff(coder->sf[cnt] - lastsf);
            lastsf += diff;
            coder->sf[cnt] = lastsf;
        }
    }
    return 1;
}

void CalcBW(unsigned *bw, int rate, SR_INFO *sr, AACQuantCfg *aacquantCfg)
{
    int max = *bw * (BLOCK_LEN_SHORT << 1) / rate, cnt, l = 0;
    for (cnt = 0; cnt < sr->num_cb_short; cnt++) {
        if (l >= max) break;
        l += sr->cb_width_short[cnt];
    }
    aacquantCfg->max_cbs = cnt;
    if (aacquantCfg->pnslevel) *bw = (faac_real)l * rate / (BLOCK_LEN_SHORT << 1);
    max = *bw * (BLOCK_LEN_LONG << 1) / rate;
    l = 0;
    for (cnt = 0; cnt < sr->num_cb_long; cnt++) {
        if (l >= max) break;
        l += sr->cb_width_long[cnt];
    }
    aacquantCfg->max_cbl = cnt;
    aacquantCfg->max_l = l;
    *bw = (faac_real)l * rate / (BLOCK_LEN_LONG << 1);
}

enum {MINSFB = 2};

static void calce(faac_real * __restrict xr, const int * __restrict bands, faac_real e[NSFB_SHORT], int maxsfb, int maxl)
{
    int sfb, l;
    for (l = maxl; l < bands[maxsfb]; l++) xr[l] = 0.0;
    for (sfb = MINSFB; sfb < maxsfb; sfb++) {
        e[sfb] = 0;
        for (l = bands[sfb]; l < bands[sfb + 1]; l++) e[sfb] += xr[l] * xr[l];
    }
}

static void resete(faac_real min[NSFB_SHORT], faac_real max[NSFB_SHORT], faac_real e[NSFB_SHORT], int maxsfb)
{
    int sfb;
    for (sfb = MINSFB; sfb < maxsfb; sfb++) min[sfb] = max[sfb] = e[sfb];
}

void BlocGroup(faac_real *xr, CoderInfo *coderInfo, AACQuantCfg *cfg)
{
    int win, sfb, win0 = 0, maxsfb = cfg->max_cbs, maxl = cfg->max_l / 8, fastmin = ((maxsfb - MINSFB) * 3) >> 2;
    faac_real e[NSFB_SHORT], min[NSFB_SHORT], max[NSFB_SHORT], thr = 3.0;
    if (coderInfo->block_type != ONLY_SHORT_WINDOW) {
        coderInfo->groups.n = 1;
        coderInfo->groups.len[0] = 1;
        return;
    }
    calce(xr, coderInfo->sfb_offset, e, maxsfb, maxl);
    resete(min, max, e, maxsfb);
    coderInfo->groups.n = 0;
    for (win = 1; win < MAX_SHORT_WINDOWS; win++) {
        int fast = 0;
        calce(xr + win * BLOCK_LEN_SHORT, coderInfo->sfb_offset, e, maxsfb, maxl);
        for (sfb = MINSFB; sfb < maxsfb; sfb++) {
            if (min[sfb] > e[sfb]) min[sfb] = e[sfb];
            if (max[sfb] < e[sfb]) max[sfb] = e[sfb];
            if (max[sfb] > thr * min[sfb]) fast++;
        }
        if (fast > fastmin) {
            coderInfo->groups.len[coderInfo->groups.n++] = win - win0;
            win0 = win;
            resete(min, max, e, maxsfb);
        }
    }
    coderInfo->groups.len[coderInfo->groups.n++] = win - win0;
}

void BlocStat(void) {}
