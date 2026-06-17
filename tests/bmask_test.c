/* bmask scaling verification test.
 * Verifies that energy and peak magnitude calculations are consistent
 * across different window grouping configurations.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include "libfaac/quantize.h"
#include "libfaac/quantize_internal.h"
#include "libfaac/coder.h"

int main(void)
{
    printf("bmask scaling verification test\n");

    CoderInfo coder;
    memset(&coder, 0, sizeof(coder));
    coder.block_type = ONLY_SHORT_WINDOW;
    coder.sfbn = 2;
    coder.sfb_offset[0] = 0;
    coder.sfb_offset[1] = 10;
    coder.sfb_offset[2] = 20;

    faac_real xr[8 * BLOCK_LEN_SHORT];
    for (int i = 0; i < 8 * BLOCK_LEN_SHORT; i++) xr[i] = 1.0f;

    faac_real bandqual[MAX_SCFAC_BANDS];
    faac_real bandenrg[MAX_SCFAC_BANDS];
    faac_real bandmaxe[MAX_SCFAC_BANDS];

    /* Case 1: single window, gsize=1 */
    coder.groups.len[0] = 1;
    bmask_test(&coder, xr, bandqual, bandenrg, bandmaxe, 0, 1.0f, 0);
    faac_real enrg1 = bandenrg[0];
    faac_real maxe1 = bandmaxe[0];
    printf("  gsize=1: enrg=%.2f, maxe=%.2f\n", enrg1, maxe1);

    /* Case 2: two windows grouped, gsize=2 */
    coder.groups.len[0] = 2;
    bmask_test(&coder, xr, bandqual, bandenrg, bandmaxe, 0, 1.0f, 0);
    faac_real enrg2 = bandenrg[0];
    faac_real maxe2 = bandmaxe[0];
    printf("  gsize=2: enrg=%.2f, maxe=%.2f\n", enrg2, maxe2);

    /* Assertions:
     * bandenrg should be sum of energies: enrg2 = 2 * enrg1
     * bandmaxe should be global peak: maxe2 = maxe1
     */
    int failed = 0;
    if (fabs(enrg2 - 2.0f * enrg1) > 1e-5) {
        printf("  FAIL: energy not summed correctly (%.2f vs expected %.2f)\n", enrg2, 2.0f * enrg1);
        failed = 1;
    }
    if (fabs(maxe2 - maxe1) > 1e-5) {
        printf("  FAIL: peak magnitude not consistent (%.2f vs expected %.2f)\n", maxe2, maxe1);
        failed = 1;
    }

    if (!failed) printf("PASS\n");
    return failed;
}
