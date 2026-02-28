import sys
import os

def patch_file(filepath, search_text, replace_text):
    if not os.path.exists(filepath):
        print(f"Error: {filepath} not found")
        return False
    with open(filepath, 'r') as f:
        content = f.read()
    if search_text not in content:
        print(f"Warning: search_text not found in {filepath}")
        return False
    new_content = content.replace(search_text, replace_text)
    with open(filepath, 'w') as f:
        f.write(new_content)
    return True

# --- Bitstream ---
bs_search1 = """  padbits = (8 - ((bitStream->numBit + 7) % 8)) % 8;"""
bs_replace1 = """  padbits = (BYTE_NUMBIT - ((bitStream->numBit + (BYTE_NUMBIT - 1)) & (BYTE_NUMBIT - 1))) & (BYTE_NUMBIT - 1);"""

bs_search2 = """    idx = (bitStream->currentBit / BYTE_NUMBIT) % bitStream->size;
    numUsed = bitStream->currentBit % BYTE_NUMBIT;
#ifndef DRM
    if (numUsed == 0)
        bitStream->data[idx] = 0;
#endif
    bitStream->data[idx] |= (data & ((1<<numBit)-1)) <<
        (BYTE_NUMBIT-numUsed-numBit);"""
bs_replace2 = """    /* Optimized by replacing division and modulo with shifts and masks */
    idx = (bitStream->currentBit >> 3) % bitStream->size;
    numUsed = bitStream->currentBit & (BYTE_NUMBIT - 1);
#ifndef DRM
    if (numUsed == 0)
        bitStream->data[idx] = 0;
#endif
    bitStream->data[idx] |= (data & ((1<<numBit)-1)) <<
        (BYTE_NUMBIT - numUsed - numBit);"""

bs_search3 = """    j = (8 - (len%8))%8;

    if ((len % 8) == 0) j = 0;"""
bs_replace3 = """    j = (BYTE_NUMBIT - (len & (BYTE_NUMBIT - 1))) & (BYTE_NUMBIT - 1);

    if ((len & (BYTE_NUMBIT - 1)) == 0) j = 0;"""

patch_file('libfaac/bitstream.c', bs_search1, bs_replace1)
patch_file('libfaac/bitstream.c', bs_search2, bs_replace2)
patch_file('libfaac/bitstream.c', bs_search3, bs_replace3)

# --- FFT ---
fft_search = """static void fft_proc(
		faac_real *xr,
		faac_real *xi,
		fftfloat *refac,
		fftfloat *imfac,
		int size)
{
	int step, shift, pos;
	int exp, estep;

	estep = size;
	for (step = 1; step < size; step *= 2)
	{
		int x1;
		int x2 = 0;
		estep >>= 1;
		for (pos = 0; pos < size; pos += (2 * step))
		{
			x1 = x2;
			x2 += step;
			exp = 0;
			for (shift = 0; shift < step; shift++)
			{
				faac_real v2r, v2i;

				v2r = xr[x2] * refac[exp] - xi[x2] * imfac[exp];
				v2i = xr[x2] * imfac[exp] + xi[x2] * refac[exp];

				xr[x2] = xr[x1] - v2r;
				xr[x1] += v2r;

				xi[x2] = xi[x1] - v2i;

				xi[x1] += v2i;

				exp += estep;

				x1++;
				x2++;
			}
		}
	}
}"""
fft_replace = """static void fft_proc(
		faac_real *xr,
		faac_real *xi,
		fftfloat *refac,
		fftfloat *imfac,
		int size)
{
	int step, shift, pos;
	int exp, estep;

	estep = size >> 1;
	/* Sur: Sur's micro-optimizations for in-order CPUs.
	   First stage: step = 1, refac[0] = 1, imfac[0] = 0.
	   Eliminate redundant multiplications by 1 and 0.
	*/
	for (pos = 0; pos < size; pos += 2)
	{
		faac_real v2r, v2i;
		int x1 = pos;
		int x2 = pos + 1;

		v2r = xr[x2];
		v2i = xi[x2];

		xr[x2] = xr[x1] - v2r;
		xr[x1] += v2r;

		xi[x2] = xi[x1] - v2i;
		xi[x1] += v2i;
	}

	/* Second stage: step = 2, estep = size / 4.
	   shift = 0: exp = 0, refac[0] = 1, imfac[0] = 0.
	   shift = 1: exp = size/4, refac[size/4] = 0, imfac[size/4] = -1.
	   Eliminate multiplications and avoid trig calls for this stage.
	*/
	if (size >= 4) {
		for (pos = 0; pos < size; pos += 4)
		{
			faac_real v2r, v2i;
			int x1 = pos;
			int x2 = pos + 2;

			/* shift = 0 */
			v2r = xr[x2];
			v2i = xi[x2];

			xr[x2] = xr[x1] - v2r;
			xr[x1] += v2r;

			xi[x2] = xi[x1] - v2i;
			xi[x1] += v2i;

			/* shift = 1 */
			x1++;
			x2++;
			v2r = xi[x2];
			v2i = -xr[x2];

			xr[x2] = xr[x1] - v2r;
			xr[x1] += v2r;

			xi[x2] = xi[x1] - v2i;
			xi[x1] += v2i;
		}
	}

	estep = size >> 2;
	for (step = 4; step < size; step *= 2)
	{
		int x1;
		int x2 = 0;
		estep >>= 1;
		for (pos = 0; pos < size; pos += (2 * step))
		{
			x1 = x2;
			x2 += step;
			exp = 0;

            /* Unrolled loop for better performance on in-order CPUs */
            for (shift = 0; shift < (step & ~3); shift += 4)
            {
                faac_real v2r, v2i, r_f, i_f;

#define FFT_BUTTERFLY(OFFSET) \
                r_f = refac[exp]; i_f = imfac[exp]; \
                v2r = xr[x2 + OFFSET] * r_f - xi[x2 + OFFSET] * i_f; \
                v2i = xr[x2 + OFFSET] * i_f + xi[x2 + OFFSET] * r_f; \
                xr[x2 + OFFSET] = xr[x1 + OFFSET] - v2r; \
                xr[x1 + OFFSET] += v2r; \
                xi[x2 + OFFSET] = xi[x1 + OFFSET] - v2i; \
                xi[x1 + OFFSET] += v2i; \
                exp += estep;

                FFT_BUTTERFLY(0)
                FFT_BUTTERFLY(1)
                FFT_BUTTERFLY(2)
                FFT_BUTTERFLY(3)
#undef FFT_BUTTERFLY

                x1 += 4;
                x2 += 4;
            }

			for (; shift < step; shift++)
			{
				faac_real v2r, v2i;
				faac_real r_f = refac[exp];
				faac_real i_f = imfac[exp];

				v2r = xr[x2] * r_f - xi[x2] * i_f;
				v2i = xr[x2] * i_f + xi[x2] * r_f;

				xr[x2] = xr[x1] - v2r;
				xr[x1] += v2r;

				xi[x2] = xi[x1] - v2i;

				xi[x1] += v2i;

				exp += estep;

				x1++;
				x2++;
			}
		}
	}
}"""
patch_file('libfaac/fft.c', fft_search, fft_replace)

# --- Filtbank (MDCT) ---
mdct_search = """    for (i = 0; i < (N >> 2); i++) {
        /* calculate real and imaginary parts of g(n) or G(p) */
        n = (N >> 1) - 1 - 2 * i;

        if (i < (N >> 3))
            tempr = data [(N >> 2) + n] + data [N + (N >> 2) - 1 - n]; /* use second form of e(n) for n = N / 2 - 1 - 2i */
        else
            tempr = data [(N >> 2) + n] - data [(N >> 2) - 1 - n]; /* use first form of e(n) for n = N / 2 - 1 - 2i */

        n = 2 * i;
        if (i < (N >> 3))
            tempi = data [(N >> 2) + n] - data [(N >> 2) - 1 - n]; /* use first form of e(n) for n=2i */
        else
            tempi = data [(N >> 2) + n] + data [N + (N >> 2) - 1 - n]; /* use second form of e(n) for n=2i*/

        /* calculate pre-twiddled FFT input */
        xr[i] = tempr * c + tempi * s;
        xi[i] = tempi * c - tempr * s;

        /* use recurrence to prepare cosine and sine for next value of i */
        cold = c;
        c = c * cfreq - s * sfreq;
        s = s * cfreq + cold * sfreq;
    }"""
mdct_replace = """/* Sur: Split MDCT loop to eliminate conditional branches inside hot loop. */
#define PRE_TWIDDLE(FORM_TEMPR, FORM_TEMPI) \
    { \
        int i2 = 2 * i; \
        n = (N >> 1) - 1 - i2; \
        tempr = FORM_TEMPR; \
        tempi = FORM_TEMPI; \
        xr[i] = tempr * c + tempi * s; \
        xi[i] = tempi * c - tempr * s; \
        cold = c; \
        c = c * cfreq - s * sfreq; \
        s = s * cfreq + cold * sfreq; \
    }

    for (i = 0; i < (N >> 3); i++) {
        PRE_TWIDDLE(data [(N >> 2) + n] + data [N + (N >> 2) - 1 - n],
                    data [(N >> 2) + i2] - data [(N >> 2) - 1 - i2])
    }
    for (; i < (N >> 2); i++) {
        PRE_TWIDDLE(data [(N >> 2) + n] - data [(N >> 2) - 1 - n],
                    data [(N >> 2) + i2] + data [N + (N >> 2) - 1 - i2])
    }
#undef PRE_TWIDDLE"""
patch_file('libfaac/filtbank.c', mdct_search, mdct_replace)

# --- Huff2 ---
huff_search = """    for (cnt = 0; cnt < len; cnt++)
    {
        int q = abs(qs[cnt]);
        if (maxq < q)
            maxq = q;
    }"""
huff_replace = """    /* Unrolled loop for faster max value search */
    for (cnt = 0; cnt < (len & ~3); cnt += 4)
    {
#define HUFF_UPDATE_MAXQ(OFFSET) \
        { \
            int q = abs(qs[cnt + OFFSET]); \
            if (maxq < q) maxq = q; \
        }
        HUFF_UPDATE_MAXQ(0)
        HUFF_UPDATE_MAXQ(1)
        HUFF_UPDATE_MAXQ(2)
        HUFF_UPDATE_MAXQ(3)
#undef HUFF_UPDATE_MAXQ
    }
    for (; cnt < len; cnt++)
    {
        int q = abs(qs[cnt]);
        if (maxq < q)
            maxq = q;
    }"""
patch_file('libfaac/huff2.c', huff_search, huff_replace)

# --- Quantize ---
quant_search = """      xr = xr0 + start;
      end -= start;
      xi = xitab;
      for (win = 0; win < gsize; win++)
      {
#ifdef __SSE2__
          if (sse2)
          {
              const __m128 zero = _mm_setzero_ps();
              const __m128 sfac = _mm_set1_ps(sfacfix);
              const __m128 magic = _mm_set1_ps(MAGIC_NUMBER);
              for (cnt = 0; cnt < end; cnt += 4)
              {
                  __m128 x = {xr[cnt], xr[cnt + 1], xr[cnt + 2], xr[cnt + 3]};

                  x = _mm_max_ps(x, _mm_sub_ps(zero, x));
                  x = _mm_mul_ps(x, sfac);
                  x = _mm_mul_ps(x, _mm_sqrt_ps(x));
                  x = _mm_sqrt_ps(x);
                  x = _mm_add_ps(x, magic);

                  *(__m128i*)(xi + cnt) = _mm_cvttps_epi32(x);
              }
              for (cnt = 0; cnt < end; cnt++)
              {
                  if (xr[cnt] < 0)
                      xi[cnt] = -xi[cnt];
              }
              xi += cnt;
              xr += BLOCK_LEN_SHORT;
              continue;
          }
#endif

          for (cnt = 0; cnt < end; cnt++)
          {
              faac_real tmp = FAAC_FABS(xr[cnt]);

              tmp *= sfacfix;
              tmp = FAAC_SQRT(tmp * FAAC_SQRT(tmp));

              xi[cnt] = (int)(tmp + MAGIC_NUMBER);
              if (xr[cnt] < 0)
                  xi[cnt] = -xi[cnt];
          }
          xi += cnt;
          xr += BLOCK_LEN_SHORT;
      }"""
quant_replace = """      xr = xr0 + start;
      end -= start;
      xi = xitab;
      if (sfacfix > 0.0) {
          /* Move constant sfacfix^0.75 out of the loop.
             Original was: (abs(xr[cnt]) * sfacfix)^0.75
             Equivalent to: abs(xr[cnt])^0.75 * sfacfix^0.75
          */
          faac_real sfacfix_075 = FAAC_POW(sfacfix, 0.75);

#ifdef __SSE2__
#ifdef FAAC_PRECISION_SINGLE
          const __m128 sfac075 = _mm_set1_ps(sfacfix_075);
          const __m128 zero = _mm_setzero_ps();
          const __m128 magic = _mm_set1_ps(MAGIC_NUMBER);
#endif
#endif

#define QUANTIZE_CORE(SAMPLE, RESULT) \
    { \
        faac_real tmp = FAAC_FABS(SAMPLE); \
        tmp = FAAC_SQRT(tmp * FAAC_SQRT(tmp)) * sfacfix_075; \
        RESULT = (int)(tmp + MAGIC_NUMBER); \
        if ((SAMPLE) < 0.0) RESULT = -RESULT; \
    }

          for (win = 0; win < gsize; win++)
          {
#ifdef __SSE2__
#ifdef FAAC_PRECISION_SINGLE
              if (sse2)
              {
                  for (cnt = 0; cnt < (end & ~3); cnt += 4)
                  {
                      __m128 x = _mm_loadu_ps((const float *)(xr + cnt));
                      x = _mm_max_ps(x, _mm_sub_ps(zero, x));
                      /* x^0.75 = sqrt(x * sqrt(x)) */
                      x = _mm_mul_ps(_mm_sqrt_ps(_mm_mul_ps(x, _mm_sqrt_ps(x))), sfac075);
                      x = _mm_add_ps(x, magic);
                      /* Use unaligned store as SFB widths are not guaranteed to be multiples of 4 */
                      _mm_storeu_si128((__m128i*)(xi + cnt), _mm_cvttps_epi32(x));
                  }
                  /* Original was: if (xr[cnt] < 0) xi[cnt] = -xi[cnt];
                     Keeping floating point comparison for performance. */
                  for (cnt = 0; cnt < end; cnt++)
                  {
                      if (xr[cnt] < 0.0f) xi[cnt] = -xi[cnt];
                  }
              }
              else
#endif
#ifdef FAAC_PRECISION_DOUBLE
              if (sse2)
              {
                  const __m128d zero_d = _mm_setzero_pd();
                  const __m128d magic_d = _mm_set1_pd(MAGIC_NUMBER);
                  const __m128d sfac075_d = _mm_set1_pd(sfacfix_075);

                  for (cnt = 0; cnt < (end & ~1); cnt += 2)
                  {
                      __m128d x = _mm_loadu_pd(xr + cnt);
                      x = _mm_max_pd(x, _mm_sub_pd(zero_d, x));
                      /* x^0.75 = sqrt(x * sqrt(x)) */
                      x = _mm_mul_pd(_mm_sqrt_pd(_mm_mul_pd(x, _mm_sqrt_pd(x))), sfac075_d);
                      x = _mm_add_pd(x, magic_d);

                      /* Convert to int and store */
                      xi[cnt] = _mm_cvttsd_si32(x);
                      xi[cnt+1] = _mm_cvttsd_si32(_mm_unpackhi_pd(x, x));
                  }
                  for (cnt = 0; cnt < end; cnt++)
                  {
                      if (xr[cnt] < 0.0) xi[cnt] = -xi[cnt];
                  }
              }
              else
#endif
#endif
              {
                  for (cnt = 0; cnt < (end & ~3); cnt += 4)
                  {
                      QUANTIZE_CORE(xr[cnt+0], xi[cnt+0])
                      QUANTIZE_CORE(xr[cnt+1], xi[cnt+1])
                      QUANTIZE_CORE(xr[cnt+2], xi[cnt+2])
                      QUANTIZE_CORE(xr[cnt+3], xi[cnt+3])
                  }
                  for (; cnt < end; cnt++)
                  {
                      QUANTIZE_CORE(xr[cnt], xi[cnt])
                  }
              }
              xi += cnt;
              xr += BLOCK_LEN_SHORT;
          }
#undef QUANTIZE_CORE
      } else {
          for (win = 0; win < gsize; win++)
          {
              for (cnt = 0; cnt < end; cnt++)
              {
                  xi[cnt] = 0;
              }
              xi += cnt;
              xr += BLOCK_LEN_SHORT;
          }
      }"""
patch_file('libfaac/quantize.c', quant_search, quant_replace)

# --- TNS ---
tns_search1 = """        /* Now filter the rest */
        for (i=length-1-order;i>=0;i--) {
            temp[i]=spec[i];
            for (j=1;j<=order;j++) {
                spec[i]+=temp[i+j]*a[j];
            }
        }"""
tns_replace1 = """        /* Now filter the rest */
        for (i=length-1-order;i>=0;i--) {
            temp[i]=spec[i];
            /* Unroll loop for better performance */
            for (j=1;j<=(order&~3);j+=4) {
#define TNS_STEP_REV(OFFSET) spec[i]+=temp[i+(j+OFFSET)]*a[j+OFFSET];
                /* Sur: Original was: spec[i]+=temp[i+j]*a[j];
                   Loop unrolling for faster DSP path. */
                TNS_STEP_REV(0)
                TNS_STEP_REV(1)
                TNS_STEP_REV(2)
                TNS_STEP_REV(3)
#undef TNS_STEP_REV
            }
            for (;j<=order;j++) {
                spec[i]+=temp[i+j]*a[j];
            }
        }"""

tns_search2 = """        /* Now filter the rest */
        for (i=order;i<length;i++) {
            temp[i]=spec[i];
            for (j=1;j<=order;j++) {
                spec[i]+=temp[i-j]*a[j];
            }
        }"""
tns_replace2 = """        /* Now filter the rest */
        for (i=order;i<length;i++) {
            temp[i]=spec[i];
            /* Unroll loop for better performance */
            for (j=1;j<=(order&~3);j+=4) {
#define TNS_STEP(OFFSET) spec[i]+=temp[i-(j+OFFSET)]*a[j+OFFSET];
                /* Sur: Original was: spec[i]+=temp[i-j]*a[j];
                   Loop unrolling for faster DSP path. */
                TNS_STEP(0)
                TNS_STEP(1)
                TNS_STEP(2)
                TNS_STEP(3)
#undef TNS_STEP
            }
            for (;j<=order;j++) {
                spec[i]+=temp[i-j]*a[j];
            }
        }"""

patch_file('libfaac/tns.c', tns_search1, tns_replace1)
patch_file('libfaac/tns.c', tns_search2, tns_replace2)

# --- Util ---
util_search = """/* Memory functions */
#define AllocMemory(size) malloc(size)
#define FreeMemory(block) free(block)
#define SetMemory(block, value, size) memset(block, value, size)"""
util_replace = """/* Memory functions */
static inline void *AllocMemory(size_t size) {
    void *ptr;
#if defined(_MSC_VER) || defined(__MINGW32__)
    ptr = _aligned_malloc(size, 16);
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
    ptr = aligned_alloc(16, (size + 15) & ~15);
#else
    if (posix_memalign(&ptr, 16, size) != 0) return NULL;
#endif
    return ptr;
}
static inline void FreeMemory(void *block) {
#if defined(_MSC_VER) || defined(__MINGW32__)
    _aligned_free(block);
#else
    free(block);
#endif
}
#define SetMemory(block, value, size) memset(block, value, size)"""
patch_file('libfaac/util.h', util_search, util_replace)
