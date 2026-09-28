/* Probe-only. See core_inject.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_inject.h"

#define CI_MAX_FRAMES 8192
#define CI_MAX_BANDS  MAX_SCFAC_BANDS

typedef struct {
    int seen;          /* 1 if this (frame,ch) had a C record */
    int win_seq;        /* raw ISO window_sequence (0-3); == FAAC block_type */
    int win_short;      /* 1 if fdk's window_sequence == EIGHT_SHORT (2) */
    int max_sfb, groups;
    int glen[8];        /* window_group_length per group; g=1 for long windows */
    int nbands;
    int cb[CI_MAX_BANDS];
    int sf[CI_MAX_BANDS];
    int ms[CI_MAX_BANDS];
    int sf_mean_set, sf_mean; /* fixed-point *256, over coded (1-11) bands */
} CIFrame;

struct CoreInject {
    unsigned fields;
    int offset;
    CIFrame (*f)[2]; /* [frame][ch] */
    unsigned long matched, total;
};

static unsigned parse_fields(const char *s)
{
    unsigned x = 0;
    char buf[128], *p, *sp;
    if (!s) return CI_CLASS;
    snprintf(buf, sizeof buf, "%s", s);
    for (p = strtok_r(buf, ",", &sp); p; p = strtok_r(NULL, ",", &sp)) {
        if (!strcmp(p, "class")) x |= CI_CLASS;
        else if (!strcmp(p, "sf")) x |= CI_SF;
        else if (!strcmp(p, "ms")) x |= CI_MS;
        else if (!strcmp(p, "win")) x |= CI_WIN;
    }
    return x ? x : CI_CLASS;
}

static void compute_sf_mean(CIFrame *fr)
{
    long sum = 0; int n = 0;
    for (int b = 0; b < fr->nbands; b++)
        if (fr->cb[b] >= 1 && fr->cb[b] <= 11) { sum += fr->sf[b]; n++; }
    fr->sf_mean = n ? (int)((sum * 256) / n) : 0;
    fr->sf_mean_set = 1;
}

struct CoreInject *CoreInjectLoad(void)
{
    const char *path = getenv("FAAC_CORE_INJECT");
    const char *fs = getenv("FAAC_CORE_INJECT_FIELDS");
    const char *offs = getenv("FAAC_CORE_INJECT_OFFSET");
    if (!path || !*path) return NULL;
    FILE *fp = fopen(path, "r");
    if (!fp) return NULL;

    struct CoreInject *in = calloc(1, sizeof(*in));
    if (!in) { fclose(fp); return NULL; }
    in->fields = parse_fields(fs);
    in->offset = offs ? (int)strtol(offs, NULL, 0) : 0;
    in->f = calloc(CI_MAX_FRAMES, sizeof(*in->f));
    if (!in->f) { free(in); fclose(fp); return NULL; }

    char line[16384];
    while (fgets(line, sizeof line, fp)) {
        if (line[0] != 'C') continue;
        unsigned frame; int ch, bits, win_seq, max_sfb, groups, gg;
        if (sscanf(line, "C %u %d %d %d %d %d %d", &frame, &ch, &bits,
                   &win_seq, &max_sfb, &groups, &gg) != 7) continue;
        if (frame >= CI_MAX_FRAMES || ch < 0 || ch > 1) continue;
        char *bar = strchr(line, '|');
        if (!bar) continue;
        CIFrame *fr = &in->f[frame][ch];
        fr->seen = 1;
        fr->win_seq = win_seq;
        fr->win_short = (win_seq == 2); /* EIGHT_SHORT_SEQUENCE */
        fr->max_sfb = max_sfb;
        fr->groups = groups;
        fr->nbands = 0;
        for (int g = 0; g < 8; g++) fr->glen[g] = 0;

        /* Trailing " g=<len0>,<len1>,..." token, appended after all '|'/'/'
         * band data by core_dump_ics; parsed from the raw line rather than
         * the tokenizer above so it can't be confused with a band token
         * (it never matches "%d:%d:%d:%d"). */
        char *gp = strstr(line, " g=");
        if (gp) {
            char gbuf[256], *tsave;
            snprintf(gbuf, sizeof gbuf, "%s", gp + 3);
            char *nl = strpbrk(gbuf, "\r\n");
            if (nl) *nl = 0;
            int ng = 0;
            for (char *t = strtok_r(gbuf, ",", &tsave); t && ng < 8;
                 t = strtok_r(NULL, ",", &tsave))
                fr->glen[ng++] = atoi(t);
        }

        char *p = bar + 1, *grp_save;
        for (char *grp = strtok_r(p, "/", &grp_save); grp && fr->groups <= groups + 8;
             grp = strtok_r(NULL, "/", &grp_save)) {
            char *tok_save, *tok;
            char gbuf[4096];
            snprintf(gbuf, sizeof gbuf, "%s", grp);
            for (tok = strtok_r(gbuf, " \t\r\n", &tok_save); tok;
                 tok = strtok_r(NULL, " \t\r\n", &tok_save)) {
                int cb, sf, nnz, ms;
                if (sscanf(tok, "%d:%d:%d:%d", &cb, &sf, &nnz, &ms) == 4) {
                    if (fr->nbands < CI_MAX_BANDS) {
                        fr->cb[fr->nbands] = cb;
                        fr->sf[fr->nbands] = sf;
                        fr->ms[fr->nbands] = ms;
                        fr->nbands++;
                    }
                }
            }
        }
        compute_sf_mean(fr);
    }
    fclose(fp);
    return in;
}

void CoreInjectFree(struct CoreInject *in)
{
    if (!in) return;
    free(in->f);
    free(in);
}

unsigned CoreInjectFields(const struct CoreInject *in) { return in ? in->fields : 0; }

int CoreInjectLookup(struct CoreInject *in, int frame, int ch, int band,
                      int is_short, int max_sfb, int num_groups,
                      const int *group_len,
                      int *cb, int *sf_shape, int *ms)
{
    if (!in) return 0;
    int n = frame + in->offset;
    if (n < 0 || n >= CI_MAX_FRAMES || ch < 0 || ch > 1) return 0;
    CIFrame *fr = &in->f[n][ch];
    if (!fr->seen) return 0;
    /* Window-layout match: short/long family, max_sfb, group count, AND the
     * exact per-group length list all agree. A long/short mismatch, a
     * differing max_sfb/groups count, or a same-count-different-boundaries
     * grouping (possible without `win` injection: two encoders can agree on
     * "3 groups" while splitting the 8 windows differently) means fdk's
     * per-band array doesn't line up with FAAC's, so decline. */
    if (fr->win_short != (is_short != 0)) return 0;
    if (fr->max_sfb != max_sfb || fr->groups != num_groups) return 0;
    if (is_short && group_len) {
        int g;
        for (g = 0; g < num_groups && g < 8; g++)
            if (fr->glen[g] != group_len[g]) return 0;
    }
    if (band < 0 || band >= fr->nbands) return 0;

    if (cb) *cb = fr->cb[band];
    if (sf_shape) *sf_shape = fr->sf[band] * 256 - fr->sf_mean; /* fixed-point *256 */
    if (ms) *ms = fr->ms[band];
    return 1;
}

int CoreInjectLookupWin(struct CoreInject *in, int frame, int ch,
                         int *win_seq, int *max_sfb, int *num_groups,
                         int *group_len)
{
    if (!in) return 0;
    int n = frame + in->offset;
    if (n < 0 || n >= CI_MAX_FRAMES || ch < 0 || ch > 1) return 0;
    CIFrame *fr = &in->f[n][ch];
    if (!fr->seen) return 0;

    if (win_seq) *win_seq = fr->win_seq;
    if (max_sfb) *max_sfb = fr->max_sfb;
    if (num_groups) *num_groups = fr->groups;
    if (group_len) {
        int g;
        for (g = 0; g < fr->groups && g < 8; g++)
            group_len[g] = fr->glen[g];
    }
    return 1;
}

void CoreInjectNoteFrame(struct CoreInject *in, int matched)
{
    if (!in) return;
    in->total++;
    if (matched) in->matched++;
}

void CoreInjectStats(const struct CoreInject *in, unsigned long *matched, unsigned long *total)
{
    if (!in) { if (matched) *matched = 0; if (total) *total = 0; return; }
    if (matched) *matched = in->matched;
    if (total) *total = in->total;
}

static struct CoreInject *g_ci;
static void core_inject_atexit(void)
{
    if (g_ci && getenv("FAAC_CORE_INJECT_DEBUG"))
        fprintf(stderr, "core_inject: matched %lu/%lu bands (fields=%u offset=%d)\n",
                g_ci->matched, g_ci->total, g_ci->fields, g_ci->offset);
}
struct CoreInject *CoreInjectGet(void)
{
    static int tried;
    if (!tried) { tried = 1; g_ci = CoreInjectLoad(); atexit(core_inject_atexit); }
    return g_ci;
}
