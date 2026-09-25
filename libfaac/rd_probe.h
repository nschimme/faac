/* Experimental ceiling probe. Included only by quantize.c with -Drd-probe=true.
 * No persistent state: each rate-control retry rebuilds everything from the
 * quantizer's original input and its current masking targets. */
typedef struct {
    double lambda;
    float xr[FRAME_LEN];
    double weight[MAX_SCFAC_BANDS];
    int bias[MAX_SCFAC_BANDS], offset[MAX_SCFAC_BANDS];
    int length[MAX_SCFAC_BANDS], original_sf[MAX_SCFAC_BANDS];
    int regular[MAX_SCFAC_BANDS], qs[FRAME_LEN];
    int costs[MAX_SCFAC_BANDS][RD_BOOKS];
} RDProbe;

static RDProbe *rd_create(void)
{
    const char *env = getenv("FAAC_RD_LAMBDA");
    char *end;
    double lambda;
    RDProbe *p;
    if (!env || !*env) return NULL;
    lambda = strtod(env, &end);
    if (*end || !isfinite(lambda) || lambda < 0) {
        fprintf(stderr, "FAAC_RD_LAMBDA must be finite and nonnegative\n"); abort();
    }
    p = calloc(1, sizeof(*p));
    if (!p) abort();
    p->lambda = lambda;
    return p;
}

static void rd_reference(RDProbe *p, const CoderInfo *c, const float *xr,
                         const BandEnergy *energy, const float *target, int g)
{
    int sb, win, offset = g ? p->offset[g*c->sfbn-1]+p->length[g*c->sfbn-1] : 0;
    for (sb = 0; sb < c->sfbn; sb++) {
        int band = g*c->sfbn+sb, lo = c->sfb_offset[sb], width = c->sfb_offset[sb+1]-lo;
        int len = width*c->groups.len[g];
        p->offset[band] = offset; p->length[band] = len; p->bias[band] = c->sf[band];
        for (win = 0; win < c->groups.len[g]; win++)
            memcpy(p->xr+offset+win*width, xr+win*BLOCK_LEN_SHORT+lo, width*sizeof(float));
        /* Existing gain before rounding is target/rms, so its reciprocal is
         * the allowed reconstruction scale. Sum squared errors in those units.
         * measure_band_energy already substitutes half the weaker L/R energy
         * for M/S, and derive_masking_targets uses the original L/R group total. */
        p->weight[band] = energy[sb].sum > 0 ? (double)target[sb]*target[sb]*len/energy[sb].sum : 0;
        offset += len;
    }
}

static double rd_error(float x, int q, double inverse_gain, double weight)
{
    double reconstructed = pow((double)abs(q), 4.0/3.0)*inverse_gain;
    double error = (q < 0 ? -reconstructed : reconstructed)-x;
    return error*error*weight;
}

static double rd_distortion(const RDProbe *p, int b, const int *qs, int sf)
{
    double d = 0, inverse_gain = 1.0/sfac_to_gain(SF_OFFSET+p->bias[b]-sf);
    int k;
    for (k = 0; k < p->length[b]; k++)
        d += rd_error(p->xr[p->offset[b]+k], qs[k], inverse_gain, p->weight[b]);
    return d;
}

/* Exact enumeration of retain/decrement choices inside each affected AAC
 * tuple. Quantized magnitudes are anchored to the ordinary quantizer at this
 * SF, so repeated sweeps cannot keep shaving the same line without limit. */
static double rd_candidate(const RDProbe *p, int b, const int *base, int sf,
                            int book, int *out, int *bits)
{
    int k, dim = book <= 4 ? 4 : 2, len = p->length[b];
    double distortion = 0, inverse_gain = 1.0/sfac_to_gain(SF_OFFSET+p->bias[b]-sf);
    *bits = 0;
    if (!book) {
        for (k = 0; k < len; k++) {
            if (abs(base[k]) > 1) return HUGE_VAL;
            out[k] = 0;
            distortion += rd_error(p->xr[p->offset[b]+k], 0, inverse_gain, p->weight[b]);
        }
        return distortion;
    }
    for (k = 0; k < len; k += dim) {
        double errors[4][2], best = HUGE_VAL, best_d = 0;
        int values[4][2], m, j, best_bits = 0, best_mask = 0;
        for (j = 0; j < dim; j++) {
            values[j][0] = base[k+j];
            values[j][1] = base[k+j] - (base[k+j] > 0) + (base[k+j] < 0);
            for (m = 0; m < 2; m++)
                errors[j][m] = rd_error(p->xr[p->offset[b]+k+j], values[j][m], inverse_gain, p->weight[b]);
        }
        for (m = 0; m < (1 << dim); m++) {
            int q[4], bits;
            double d = 0, value;
            for (j = 0; j < dim; j++) { q[j] = values[j][(m >> j)&1]; d += errors[j][(m >> j)&1]; }
            bits = rd_tuple_bits(q, dim, book);
            if (bits >= RD_INF) continue;
            value = d+p->lambda*bits;
            if (value < best) { best = value; best_d = d; best_bits = bits; best_mask = m; }
        }
        if (!isfinite(best)) return HUGE_VAL;
        for (j = 0; j < dim; j++) out[k+j] = values[j][(best_mask >> j)&1];
        distortion += best_d; *bits += best_bits;
    }
    return distortion;
}

static int rd_spectral(const CoderInfo *c, const RDProbe *p)
{
    int b, bits = 0;
    for (b = 0; b < c->bandcnt; b++) bits += p->costs[b][c->book[b]];
    return bits;
}

static void rd_optimize(RDProbe *p, CoderInfo *c, const int *packed)
{
    int b, k, off = 0, pass, changed, accepted = 0;
    int base[FRAME_LEN], candidate[FRAME_LEN], bestq[FRAME_LEN];
    for (b = 0; b < c->bandcnt; b++) {
        int book = c->book[b];
        p->original_sf[b] = c->sf[b];
        p->regular[b] = book >= HCB_1 && book <= HCB_ESC;
        if (p->regular[b]) {
            memcpy(p->qs+p->offset[b], packed+off, p->length[b]*sizeof(int));
            off += p->length[b];
            rd_band_costs(p->qs+p->offset[b], p->length[b], p->costs[b]);
        } else {
            for (k = 0; k < RD_BOOKS; k++) p->costs[b][k] = RD_INF;
            p->costs[b][book] = 0;
        }
    }
    rd_select_books(c, p->costs);
    int original_bits = rd_spectral(c, p)+rd_sections(c)+rd_scalefactors(c);
    /* Eight deterministic forward coordinate sweeps. Usually converges in
     * two; bounded for pathological inputs. Incumbent wins every exact tie. */
    for (pass = 0; pass < 8; pass++) {
        changed = 0;
        for (b = 0; b < c->bandcnt; b++) if (p->regular[b]) {
            int oldbook = c->book[b], oldsf = c->sf[b], bestbook = oldbook, bestsf = oldsf;
            int len = p->length[b], rest = rd_spectral(c, p)-p->costs[b][oldbook];
            double best = rd_distortion(p, b, p->qs+p->offset[b], oldsf)
                +p->lambda*(rest+p->costs[b][oldbook]+rd_sections(c)+rd_scalefactors(c));
            int delta, book;
            memcpy(bestq, p->qs+p->offset[b], len*sizeof(int));
            for (delta = -2; delta <= 2; delta++) {
                int sf = p->original_sf[b]+delta;
                if (sf < 0 || sf > 255) continue;
                qfunc(p->xr+p->offset[b], base, len/4, sfac_to_gain(SF_OFFSET+p->bias[b]-sf));
                for (book = 0; book <= HCB_ESC; book++) {
                    int bits, sf_bits, actual_book = book, nonzero = 0;
                    double d = rd_candidate(p, b, base, sf, book, candidate, &bits), value;
                    if (!isfinite(d)) continue;
                    for (k = 0; k < len; k++) nonzero |= candidate[k];
                    if (!nonzero) { actual_book = 0; bits = 0; }
                    c->book[b] = actual_book; c->sf[b] = sf;
                    sf_bits = rd_scalefactors(c);
                    if (sf_bits >= RD_INF) continue;
                    value = d+p->lambda*(rest+bits+rd_sections(c)+sf_bits);
                    if (value < best-1e-9*(1+fabs(best))) {
                        best = value; bestsf = sf; bestbook = actual_book;
                        memcpy(bestq, candidate, len*sizeof(int));
                    }
                }
            }
            c->book[b] = bestbook; c->sf[b] = bestsf;
            if (bestbook != oldbook || bestsf != oldsf || memcmp(bestq, p->qs+p->offset[b], len*sizeof(int))) {
                memcpy(p->qs+p->offset[b], bestq, len*sizeof(int));
                rd_band_costs(bestq, len, p->costs[b]);
                rd_select_books(c, p->costs);
                changed = 1; accepted++;
            }
            if (rd_scalefactors(c) >= RD_INF) abort();
        }
        if (!changed) break;
    }
    rd_emit(c, p->qs, p->offset, p->costs);
    if (getenv("FAAC_RD_TRACE"))
        fprintf(stderr, "RD lambda=%.9g passes=%d accepted=%d bits=%d->%d\n", p->lambda, pass+1,
                accepted, original_bits, rd_spectral(c,p)+rd_sections(c)+rd_scalefactors(c));
}
