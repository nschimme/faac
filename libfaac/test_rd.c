/* Probe accounting tests: independent enumeration and actual bit writing. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "coder.h"
#include "huff2.h"

static void init(CoderInfo *c, int *offsets, int n, int shortwin, int groups)
{
    int b;
    memset(c, 0, sizeof(*c));
    c->sfbn = n; c->groups.n = groups; c->bandcnt = n*groups;
    c->block_type = shortwin ? ONLY_SHORT_WINDOW : ONLY_LONG_WINDOW;
    c->sfb_offset = offsets;
    for (b = 0; b <= n; b++) offsets[b] = b*4;
    for (b = 0; b < groups; b++) c->groups.len[b] = 1;
    for (b = 0; b < c->bandcnt; b++) c->sf[b] = 100;
}

int main(void)
{
    CoderInfo c;
    int offsets[NSFB_LONG+1], costs[MAX_SCFAC_BANDS][RD_BOOKS];
    int q[FRAME_LEN], qo[MAX_SCFAC_BANDS], b, k, v, n;
    const int values[] = {1,2,3,4,5,7,8,12,13,15,16,17,31,32,63,64,8190,8191};
    for (v = 0; v < (int)(sizeof(values)/sizeof(*values)); v++) {
        init(&c, offsets, 1, 0, 1); qo[0] = 0;
        q[0] = values[v]; q[1] = -values[v]; q[2] = 0; q[3] = 1;
        rd_band_costs(q, 4, costs[0]);
        for (k = 1; k <= 11; k++) if (costs[0][k] < RD_INF) {
            int tuple = k <= 4 ? rd_tuple_bits(q, 4, k)
                : rd_tuple_bits(q, 2, k)+rd_tuple_bits(q+2, 2, k);
            assert(tuple == costs[0][k]);
            c.book[0] = k;
            rd_emit(&c, q, qo, costs);
        }
    }
    /* Exact multiples pay a terminating zero run field. */
    for (n = 6; n <= 32; n++) {
        for (int shortwin = 0; shortwin <= 1; shortwin++) {
            if (shortwin && n > 15) continue;
            init(&c, offsets, n, shortwin, shortwin ? 2 : 1);
            int rb = shortwin ? 3 : 5, run = (1 << rb)-1;
            for (b = 0; b < c.bandcnt; b++) {
                qo[b] = 4*b; q[4*b] = 1; q[4*b+1] = -1; q[4*b+2] = q[4*b+3] = 0;
                rd_band_costs(q+4*b, 4, costs[b]); c.book[b] = 1;
            }
            assert(rd_sections(&c) == c.groups.n*(4+rb*(1+n/run)));
            rd_emit(&c, q, qo, costs);
        }
    }
    /* Independent exhaustive oracle for section selection, including runs
     * that cross the short-window length-escape boundary. */
    for (int seed = 0; seed < 100; seed++) {
        init(&c, offsets, 10, 1, 1);
        for (b = 0; b < 10; b++) {
            for (k = 0; k < RD_BOOKS; k++) costs[b][k] = RD_INF;
            costs[b][1] = (seed*7+b*13)%17;
            costs[b][3] = (seed*19+b*3)%23;
        }
        int best = RD_INF;
        for (int mask = 0; mask < 1024; mask++) {
            int bits = 0;
            for (b = 0; b < 10; b++) { c.book[b] = mask&(1 << b) ? 1 : 3; bits += costs[b][c.book[b]]; }
            bits += rd_sections(&c);
            if (bits < best) best = bits;
        }
        assert(rd_select_books(&c, costs) == best);
    }
    init(&c, offsets, 3, 0, 1);
    c.book[0] = c.book[1] = c.book[2] = 1;
    c.sf[0] = 0; c.sf[1] = 60; c.sf[2] = 120;
    assert(rd_scalefactors(&c) < RD_INF);
    c.book[1] = 0; assert(rd_scalefactors(&c) == RD_INF);
    c.book[0] = 13; c.sf[0] = -227; c.sf[2] = 255;
    assert(rd_scalefactors(&c) == RD_INF);
    init(&c, offsets, 3, 0, 1);
    memset(q, 0, sizeof(q));
    for (b = 0; b < 3; b++) { qo[b] = 4*b; rd_band_costs(q+4*b, 4, costs[b]); }
    rd_select_books(&c, costs); rd_emit(&c, q, qo, costs);
    puts("RD accounting: tuples, escapes, group runs, exhaustive section oracle, SF limits and zero bands passed");
    return 0;
}
