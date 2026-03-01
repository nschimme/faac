/*
Copyright (c) 2003-2004, Mark Borgerding

All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
    * Neither the author nor the names of any contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/* kiss_fft.h
   defines kiss_fft_scalar as either short or a faac_real type
   and defines
   typedef struct { kiss_fft_scalar r; kiss_fft_scalar i; }kiss_fft_cpx; */
#include "kiss_fft.h"


#define MAXFACTORS 32
/* e.g. an fft of length 128 has 4 factors 
 as far as kissfft is concerned
 4*4*4*2
 */

struct kiss_fft_state{
    int nfft;
    int inverse;
    int factors[2*MAXFACTORS];
    kiss_fft_cpx twiddles[1];
};

/*
  Explanation of macros dealing with complex math:

   C_MUL(m,a,b)         : m = a*b
   C_FIXDIV( c , div )  : if a fixed point impl., c /= div. noop otherwise
   C_SUB( res, a,b)     : res = a - b
   C_SUBFROM( res , a)  : res -= a
   C_ADDTO( res , a)    : res += a
 * */
#ifdef FIXED_POINT

#   define smul(a,b) ( (long)(a)*(b) )
#   define sround( x )  (short)( ( (x) + (1<<14) ) >>15 )

#   define S_MUL(a,b) sround( smul(a,b) )

#ifdef CPUMXU
#include "../mxu_macros.h"
#define C_MUL(m,a,b) \
    do { \
        int res_r, res_i; \
        int val_a = ((int)((a).r) << 16) | ((int)((a).i) & 0xffff); \
        int val_b = ((int)((b).r) << 16) | ((int)((b).i) & 0xffff); \
        __asm__ __volatile__ ( \
            "move $2, %2\n\t" \
            "move $3, %3\n\t" \
            MXU_S32I2M(1, 2) \
            MXU_S32I2M(2, 3) \
            MXU_D16MUL(3, 1, 2, 4, 0) /* XR3 = ac, XR4 = bd */ \
            MXU_D16MUL(5, 1, 2, 6, 3) /* XR5 = ad, XR6 = bc */ \
            MXU_S32M2I(3, 2) \
            MXU_S32M2I(4, 3) \
            "subu %0, $2, $3\n\t" \
            MXU_S32M2I(5, 2) \
            MXU_S32M2I(6, 3) \
            "addu %1, $2, $3\n\t" \
            "addiu %0, %0, 16384\n\t" \
            "addiu %1, %1, 16384\n\t" \
            : "=r"(res_r), "=r"(res_i) \
            : "r"(val_a), "r"(val_b) \
            : "$2", "$3" \
        ); \
        (m).r = (kiss_fft_scalar)(res_r >> 15); \
        (m).i = (kiss_fft_scalar)(res_i >> 15); \
    } while(0)
#else
#   define C_MUL(m,a,b) \
      do{ (m).r = sround( smul((a).r,(b).r) - smul((a).i,(b).i) ); \
          (m).i = sround( smul((a).r,(b).i) + smul((a).i,(b).r) ); }while(0)
#endif

#   define C_FIXDIV(c,div) \
    do{ (c).r /= div; (c).i /=div; }while(0)

#   define C_MULBYSCALAR( c, s ) \
    do{ (c).r =  sround( smul( (c).r , s ) ) ;\
        (c).i =  sround( smul( (c).i , s ) ) ; }while(0)

#else  /* not FIXED_POINT*/

#   define S_MUL(a,b) ( (a)*(b) )
#ifdef CPUMXU
#include "../mxu_macros.h"
#define C_MUL(m,a,b) \
    do { \
        float ar = (float)(a).r; \
        float ai = (float)(a).i; \
        float br = (float)(b).r; \
        float bi = (float)(b).i; \
        float resr, resi; \
        __asm__ __volatile__ ( \
            "mtc1 %2, $f4\n\t" \
            "mtc1 %3, $f5\n\t" \
            "mtc1 %4, $f6\n\t" \
            "mtc1 %5, $f7\n\t" \
            "mfc1 $2, $f4\n\t" \
            "mfc1 $3, $f5\n\t" \
            MXU_S32I2M(1, 2) \
            MXU_S32I2M(2, 3) \
            MXU_REPIW(1, 1, 0) /* VR1 = [ar, ar, ar, ar] */ \
            MXU_REPIW(2, 2, 0) /* VR2 = [ai, ai, ai, ai] */ \
            "mfc1 $2, $f6\n\t" \
            "mfc1 $3, $f7\n\t" \
            MXU_S32I2M(3, 2) \
            MXU_S32I2M(4, 3) \
            MXU_REPIW(3, 3, 0) /* VR3 = [br, br, br, br] */ \
            MXU_REPIW(4, 4, 0) /* VR4 = [bi, bi, bi, bi] */ \
            MXU_FMULW(5, 1, 3) /* VR5 = ar*br */ \
            MXU_FMULW(6, 2, 4) /* VR6 = ai*bi */ \
            MXU_FSUBW(7, 5, 6) /* VR7 = ar*br - ai*bi */ \
            MXU_FMULW(8, 1, 4) /* VR8 = ar*bi */ \
            MXU_FMULW(9, 2, 3) /* VR9 = ai*br */ \
            MXU_FADDW(10, 8, 9) /* VR10 = ar*bi + ai*br */ \
            MXU_MTFPUW($f4, 7, 0) \
            MXU_MTFPUW($f5, 10, 0) \
            "mfc1 $2, $f4\n\t" \
            "mfc1 $3, $f5\n\t" \
            "mtc1 $2, %0\n\t" \
            "mtc1 $3, %1\n\t" \
            : "=f"(resr), "=f"(resi) \
            : "f"(ar), "f"(ai), "f"(br), "f"(bi) \
            : "$2", "$3", "$f4", "$f5", "$f6", "$f7" \
        ); \
        (m).r = (kiss_fft_scalar)resr; \
        (m).i = (kiss_fft_scalar)resi; \
    } while(0)
#else
#define C_MUL(m,a,b) \
    do{ (m).r = (a).r*(b).r - (a).i*(b).i;\
        (m).i = (a).r*(b).i + (a).i*(b).r; }while(0)
#endif
#   define C_FIXDIV(c,div) /* NOOP */
#   define C_MULBYSCALAR( c, s ) \
    do{ (c).r *= (s);\
        (c).i *= (s); }while(0)
#endif

#define  C_ADD( res, a,b)\
    do {    (res).r=(a).r+(b).r;  (res).i=(a).i+(b).i;  }while(0)
#define  C_SUB( res, a,b)\
    do {    (res).r=(a).r-(b).r;  (res).i=(a).i-(b).i;  }while(0)
#define C_ADDTO( res , a)\
    do {    (res).r += (a).r;  (res).i += (a).i;  }while(0)
#define C_SUBFROM( res , a)\
    do {    (res).r -= (a).r;  (res).i -= (a).i;  }while(0)

static 
void kf_cexp(kiss_fft_cpx * x,faac_real phase) /* returns e ** (j*phase)   */
{
#ifdef FIXED_POINT
    x->r = (kiss_fft_scalar) (32767 * FAAC_COS(phase));
    x->i = (kiss_fft_scalar) (32767 * FAAC_SIN(phase));
#else
    x->r = FAAC_COS(phase);
    x->i = FAAC_SIN(phase);
#endif
}

/* a debugging function */
#define pcpx(c)\
    fprintf(stderr,"%g + %gi\n",(faac_real)((c)->r),(faac_real)((c)->i) )
