#include "quantize_internal.h"
#include <math.h>

#define MAGIC_NUMBER 0.4054

void quantize_lines(int end, faac_real sfacfix, const faac_real *xr, int *xi)
{
    int cnt;
    for (cnt = 0; cnt < end; cnt++)
    {
        faac_real tmp = FAAC_FABS(xr[cnt]);

        tmp *= sfacfix;
        tmp = FAAC_SQRT(tmp * FAAC_SQRT(tmp));

        xi[cnt] = (int)(tmp + MAGIC_NUMBER);
        if (xr[cnt] < 0)
            xi[cnt] = -xi[cnt];
    }
}

/* No stubs here, we will use #if in the arch files to only define them when appropriate */
