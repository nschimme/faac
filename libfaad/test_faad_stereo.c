/* Intensity polarity: mode 2 has an implicit all-one M/S mask, including
 * intensity bands. Mode 0 ignores ms_used; mode 1 uses its explicit flags. */
#include "faad_internal.h"
#include <stdio.h>
#include <string.h>

void init_is_tables(void);

#ifdef FAAD_STATS
faadDecStats g_faadStats;
#endif

static int check(unsigned cb, unsigned mask, unsigned used, int pos, int short_windows)
{
    CPEInfo cpe = {0};
    ICSInfo *ics = &cpe.ics[1];
    float left[1024], right[1024];
    cpe.ms_mask_present = mask;
    ics->frame_len_long = 1024;
    ics->num_window_groups = short_windows ? 2 : 1;
    ics->window_group_length[0] = short_windows ? 3 : 1;
    ics->window_group_length[1] = 5;
    ics->max_sfb = 1;
    ics->sfb_offsets[0] = 4;
    ics->sfb_offsets[1] = 8;
    for (int g = 0; g < ics->num_window_groups; g++) {
        ics->sfb_cb[g][0] = cb;
        ics->scalefactors[g][0] = pos;
        cpe.ms_used[g][0] = used;
    }
    for (int k = 0; k < 1024; k++) {
        left[k] = (float)(k % 17 - 8);
        right[k] = 123.0f;
    }
    float original[1024];
    memcpy(original, left, sizeof left);
    apply_is_stereo(&cpe, left, right);

    /* Literal expected signs for codebook 14; codebook 15 is opposite.
     * Columns: mode 0, mode 1 flag 0, mode 1 flag 1, mode 2. */
    static const int signs[4] = {-1, -1, 1, 1};
    unsigned column = mask == 0 ? 0 : mask == 2 ? 3 : 1 + used;
    float scale = signs[column] * (cb == 14 ? 1.0f : -1.0f);
    scale *= pos == -4 ? 2.0f : pos == 4 ? 0.5f : 1.0f;
    for (int k = 0; k < 1024; k++) {
        int line = short_windows ? k % 128 : k;
        float expected = line >= 4 && line < 8 ? original[k] * scale : 123.0f;
        if (right[k] != expected || left[k] != original[k]) {
            fprintf(stderr, "IS cb=%u mask=%u used=%u pos=%d short=%d line=%d: got %g, expected %g\n",
                    cb, mask, used, pos, short_windows, k, right[k], expected);
            return 1;
        }
    }
    return 0;
}

int main(void)
{
    init_is_tables();
    int failed = 0;
    for (unsigned cb = 14; cb <= 15; cb++)
        for (unsigned mask = 0; mask <= 2; mask++)
            for (unsigned used = 0; used <= 1; used++)
                for (int pos = -4; pos <= 4; pos += 4)
                    for (int short_windows = 0; short_windows <= 1; short_windows++)
                        failed |= check(cb, mask, used, pos, short_windows);
    if (!failed) puts("Intensity stereo polarity: PASS");
    return failed;
}
