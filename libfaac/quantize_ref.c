#include "quantize.h"
#include <math.h>

#define MAGIC_NUMBER 0.4054

void quantize_sfb(int end, int gsize, faac_real sfacfix, const faac_real *xr, int *xi)
{
    int win, cnt;
    for (win = 0; win < gsize; win++)
    {
        for (cnt = 0; cnt < end; cnt++)
        {
            faac_real tmp = FAAC_FABS(xr[cnt]);

            tmp *= sfacfix;
            tmp = FAAC_SQRT(tmp * FAAC_SQRT(tmp));

            xi[cnt] = (int)(tmp + MAGIC_NUMBER);
            if (xr[cnt] < 0)
                xi[cnt] = -xi[cnt];
        }
        xi += end;
        xr += BLOCK_LEN_SHORT;
    }
}
