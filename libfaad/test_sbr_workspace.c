/* Compare shared storage with the separate-buffer processing order. */
#include "sbr.c"
#include <assert.h>
#include <stdio.h>

static uint32_t seed = 1234567;

static float sample(void)
{
    seed = seed * 1664525u + 1013904223u;
    return ((int)(seed >> 16) - 32768) / 32768.0f;
}

static void sbr_assemble_reference(const SBRElement *el, SBRChannel *ch, SBRScratch *sc, bool have_hf, int nslots, float *pcm, float (*output_slot)[2])
{
    int i_temp = have_hf ? (int)ch->t_E_end_prev - SBR_SLOTS : 0;
    if (i_temp < 0) i_temp = 0;
    for (int i = 0; i < nslots; i++) {
        float (*slot)[2] = pcm ? output_slot : output_slot + i * 64;
        int n = i + SBR_T_HFADJ;
        int kx = have_hf ? ((i < i_temp) ? ch->kx_prev : el->kx) : 32;
        int kend = have_hf ? ((i < i_temp) ? ch->kx_prev + ch->M_prev : el->kx + el->M) : 32;
        if (i >= SBR_SLOTS) kend = kx; /* look-ahead slots: low band only */
        for (int k = 0; k < kx && k < 32; k++) {
            slot[k][0] = sc->x_low[k][n][0];
            slot[k][1] = sc->x_low[k][n][1];
        }
        for (int k = kx; k < kend; k++) {
            slot[k][0] = sc->y[k][n][0];
            slot[k][1] = sc->y[k][n][1];
        }
        for (int k = kend; k < 64; k++) slot[k][0] = slot[k][1] = 0.0f;
        if (pcm) qmf_synthesis_slot(ch, slot, pcm + i * 64);
    }
}

static void sbr_process_reference(const SBRElement *el, SBRChannel *ch, SBRScratch *sc, const float *pcm,
                                float E[SBR_MAX_ENV][SBR_MAX_BANDS], float Q[2][SBR_MAX_NQ], bool have_hf, int nslots, float *out_pcm, float (*output_slot)[2],
                                float tail[SBR_MAX_BANDS][SBR_T_HFGEN][2])
{
    sbr_analyse(ch, sc, pcm);

    /* Y carries the previous frame's tail; the HF generator then writes
     * the region this frame adjusts, the adjuster rewrites it in place
     * (each envelope's energy is read before its slots are scaled), and
     * the rest stays zero. */
    memset(sc->y, 0, sizeof(float[SBR_MAX_BANDS][SBR_BUF_SLOTS][2]));
    for (int k = 0; k < SBR_MAX_BANDS; k++)
        memcpy(sc->y[k], tail[k], sizeof(tail[k]));

    if (have_hf) {
        sbr_chirp(el, ch);
        sbr_hf_generate(el, ch, sc);
        sbr_hf_adjust(el, ch, sc, E, Q);
    }

    sbr_assemble_reference(el, ch, sc, have_hf, nslots, out_pcm, output_slot);

    for (int k = 0; k < SBR_MAX_BANDS; k++) {
        memcpy(tail[k], &sc->y[k][SBR_SLOTS], sizeof(tail[k]));
        memcpy(ch->y_tail[k], tail[k] + SBR_T_HFADJ, sizeof(ch->y_tail[k]));
    }

    if (have_hf) {
        /* remember what the next frame's leading slots and deltas refer to */
        int last = ch->L_E - 1;
        int nb = ch->freq_res[last] ? el->n_high : el->n_low;
        memcpy(ch->E_prev, ch->E[last], sizeof(int16_t) * (size_t)nb);
        ch->freq_res_prev = ch->freq_res[last];
        memcpy(ch->Q_prev, ch->Q[ch->L_Q - 1], sizeof(int16_t) * el->n_q);
        ch->l_A_prev = ch->l_A;
        ch->L_E_prev = ch->L_E;
        ch->t_E_end_prev = (uint8_t)(2 * ch->t_E[ch->L_E]);
        ch->kx_prev = el->kx;
        ch->M_prev = el->M;
    }
}

#ifndef FAAD_DISABLE_PS
/* The low-band prepass must match full assembly even when the crossover
 * lies inside the five hybrid inputs, including the preceding frame's tail. */
static void check_hybrid_input(void)
{
    float shared[SBR_MAX_BANDS][SBR_BUF_SLOTS][2];
    float low[32][SBR_BUF_SLOTS][2];
    float high[SBR_MAX_BANDS][SBR_BUF_SLOTS][2];
    float head[32][SBR_T_HFGEN][2];
    float frame[PS_IN_SLOTS][64][2];
    SBRChannel ch = {0};
    SBRScratch sc = { .x_low = shared, .y = shared };
    SBRScratch reference = { .x_low = low, .y = high };
    for (int k = 0; k < SBR_MAX_BANDS; k++)
        for (int n = 0; n < SBR_BUF_SLOTS; n++)
            for (int z = 0; z < 2; z++) shared[k][n][z] = sample();
    for (int k = 0; k < 32; k++)
        for (int n = 0; n < SBR_T_HFGEN; n++)
            for (int z = 0; z < 2; z++) head[k][n][z] = sample();
    for (int k = 0; k < SBR_MAX_BANDS; k++)
        for (int n = 0; n < SBR_HF_TAIL_SLOTS; n++)
            for (int z = 0; z < 2; z++) ch.y_tail[k][n][z] = sample();
    for (int current = 1; current <= 32; current++)
    for (int previous = 1; previous <= 32; previous++)
    for (int hf = 0; hf <= 1; hf++) {
        SBRElement el = { .kx = current, .M = 24 };
        ch.kx_prev = previous;
        ch.M_prev = 24;
        ch.t_E_end_prev = 38;
        memcpy(low, shared, sizeof(low));
        memcpy(high, shared, sizeof(high));
        if (hf) {
            for (int k = current; k < 32; k++)
                memcpy(low[k], head[k], sizeof(head[k]));
            for (int k = 0; k < current; k++) {
                memset(high[k], 0, sizeof(high[k]));
                memcpy(high[k] + SBR_T_HFADJ, ch.y_tail[k], sizeof(ch.y_tail[k]));
            }
        }
        sbr_assemble_reference(&el, &ch, &reference, hf, PS_IN_SLOTS,
                               NULL, (float (*)[2])frame);
        for (int i = 0; i < PS_IN_SLOTS; i++) {
            float input[6][2];
            for (int k = 0; k < 6; k++) input[k][0] = input[k][1] = 123.0f;
            sbr_assemble_hybrid_input(&el, &ch, &sc, hf, i, input, head);
            assert(memcmp(input, frame[i], sizeof(float[5][2])) == 0);
            assert(input[5][0] == 123.0f && input[5][1] == 123.0f);
        }
    }
}

#if MAX_CHANNELS >= 2
static void check_ps_streaming(void)
{
    faad_config cfg;
    faad_config_init(&cfg, sizeof(cfg));
    faad_decoder *dec = NULL, *control = NULL;
    assert(faad_decoder_open(&cfg, NULL, 0, &dec) == FAAD_OK);
    assert(faad_decoder_open(&cfg, NULL, 0, &control) == FAAD_OK);
    assert(faad_ensure_sbr(dec) == FAAD_OK && faad_ensure_ps(dec) == FAAD_OK);
    assert(faad_ensure_sbr(control) == FAAD_OK && faad_ensure_ps(control) == FAAD_OK);
    dec->ps_seen = control->ps_seen = true;
    SBRScratch scratch = {0};
    scratch.x_low = calloc(32, sizeof(*scratch.x_low));
    scratch.y = calloc(SBR_MAX_BANDS, sizeof(*scratch.y));
    float (*frame)[64][2] = calloc(PS_IN_SLOTS, sizeof(*frame));
    assert(scratch.x_low && scratch.y && frame);
    float tail[SBR_MAX_BANDS][SBR_T_HFGEN][2] = {0};
    float E[SBR_MAX_ENV][SBR_MAX_BANDS] = {0};
    float Q[2][SBR_MAX_NQ] = {0};
    /* Start without PS data, then exercise both layout transitions with
     * persistent hybrid and decorrelator history. */
    for (int pass = 0; pass < 4; pass++) {
        PSState *ps = dec->ps;
        ps->start = pass != 0;
        ps->is34 = pass == 2;
        ps->num_env = 1;
        ps->border[0] = -1;
        ps->border[1] = 31;
        ps->nr_iid_par = ps->nr_icc_par = ps->is34 ? 34 : 20;
        for (int k = 0; k < PS_NR_PAR; k++) {
            ps->iid_par[0][k] = (k % 5) - 2;
            ps->icc_par[0][k] = k % 8;
        }
        memcpy(control->ps, ps, sizeof(*ps));
        for (int i = 0; i < FRAME_LEN_LONG; i++)
            dec->pcm[i] = control->pcm[i] = sample();
        sbr_process_reference(&control->sbr_el[0], &control->sbr[0], &scratch,
                              control->pcm, E, Q, false, PS_IN_SLOTS, NULL,
                              (float (*)[2])frame, tail);
        if (control->ps->start) {
            for (int k = 0; k < 5; k++)
                for (int i = 0; i < PS_IN_SLOTS; i++)
                    memcpy(control->ps->in_buf[k][i + 6], frame[i][k], sizeof(float[2]));
            ps_frame_begin(control, 32);
        }
        for (int i = 0; i < SBR_SLOTS; i++) {
            float L[64][2], R[64][2];
            float (*left)[2] = frame[i], (*right)[2] = frame[i];
            if (control->ps->start) {
                ps_slot(control, i, frame[i], L, R);
                left = L;
                right = R;
            }
            qmf_synthesis_slot(&control->sbr[0], left, control->pcm + i * 64);
            qmf_synthesis_slot(&control->sbr[1], right, control->pcm + 2048 + i * 64);
        }
        sbr_apply(dec, 1, dec->pcm);
        assert(memcmp(dec->pcm, control->pcm, sizeof(float[4096])) == 0);
        assert(memcmp(dec->ps, control->ps, sizeof(*ps)) == 0);
        assert(memcmp(dec->sbr, control->sbr, sizeof(SBRChannel[2])) == 0);
    }
    free(scratch.x_low);
    free(scratch.y);
    free(frame);
    faad_decoder_close(&dec);
    faad_decoder_close(&control);
}
#endif
#endif

int main(void)
{
#ifndef FAAD_DISABLE_PS
    check_hybrid_input();
#if MAX_CHANNELS >= 2
    check_ps_streaming();
#endif
#endif
    SBRElement tables[2048];
    int nt = 0;
    for (int start = 0; start < 16; start++)
    for (int stop = 0; stop < 16; stop++)
    for (int xo = 0; xo < 4; xo++) {
        SBRElement el = {0};
        el.start_freq = start;
        el.stop_freq = stop;
        el.xover_band = xo;
        el.freq_scale = 2;
        el.alter_scale = 1;
        el.noise_bands = 2;
        el.limiter_bands = 2;
        el.limiter_gains = 1;
        if (sbr_build_tables(&el, 44100)) {
            assert(nt < 2048);
            tables[nt++] = el;
        }
    }
    faad_config cfg;
    faad_config_init(&cfg, sizeof(cfg));
    faad_decoder *dec = NULL;
    assert(faad_decoder_open(&cfg, NULL, 0, &dec) == FAAD_OK);
    assert(faad_ensure_sbr(dec) == FAAD_OK);
    float *output = dec->pcm;
    SBRScratch reference = {0};
    reference.x_low = calloc(32, sizeof(*reference.x_low));
    reference.y = calloc(SBR_MAX_BANDS, sizeof(*reference.y));
    float (*reference_frame)[64][2] = calloc(PS_IN_SLOTS, sizeof(*reference_frame));
    float (*candidate_frame)[64][2] = calloc(PS_IN_SLOTS, sizeof(*candidate_frame));
    SBRChannel *saved = malloc(sizeof(*saved));
    float *pcm_ref = malloc(sizeof(float[2048]));
    assert(reference.x_low && reference.y && reference_frame && candidate_frame && saved && pcm_ref);
    size_t cases = 0, up = 0, down = 0;
    for (int t = 0;t < nt; t++) for (int mode = 0;mode < 8; mode++) {
        SBRElement el = tables[t],prev = tables[(t*7+3)%nt];
        el.interpol_freq = (mode>>1)&1;
        el.smoothing_mode = (mode>>2)&1;
        SBRChannel *ch = &dec->sbr[0];
        memset(ch, 0,  sizeof(*ch));
        ch->L_E = 2;
        ch->L_Q = 2;
        ch->t_E[0] = mode&3;
        ch->t_E[1] = 8;
        ch->t_E[2] = 16+(mode&3);
        ch->t_Q[0] = ch->t_E[0];
        ch->t_Q[1] = 8;
        ch->t_Q[2] = ch->t_E[2];
        ch->freq_res[0] = mode&1;
        ch->freq_res[1] = 1;
        ch->l_A = -1;
        ch->l_A_prev = -1;
        ch->kx_prev = prev.kx;
        ch->M_prev = prev.M;
        ch->t_E_end_prev = 32+2*(mode&3);
        ch->primed = true;
        ch->L_E_prev = 2;
        ch->hist_pos = mode&3;
        for (int k = 0;k < 32; k++) for (int n = 0;n < 8; n++) for (int z = 0;z < 2; z++) ch->x_low_tail[k][n][z] = sample();
        for (int k = prev.kx;k < prev.kx+prev.M; k++) for (int n = 0;n < SBR_HF_TAIL_SLOTS; n++) for (int z = 0;z < 2; z++) ch->y_tail[k][n][z] = sample();
        for (int h = 0;h < 4; h++) for (int k = 0;k < 64; k++) {ch->g_hist[h][k] = 0.5f;
            ch->q_hist[h][k] = 0.025f;
        }
        for (int k = 0;k < 5; k++) ch->invf_mode[k] = mode&3;
        ch->add_harmonic_flag = true;
        for (int k = 0;k < el.n_high; k++) ch->add_harmonic[k] = (k+mode)%3 == 0;
        for (int k = 0;k < 1024; k++) output[k] = sample();
        float E[5][64],Q[2][5];
        for (int l = 0;l < 5; l++) for (int k = 0;k < 64; k++) E[l][k] = 1+(k%7);
        for (int l = 0;l < 2; l++) for (int k = 0;k < 5; k++) Q[l][k] = 0.125f;
        bool hf = (mode!=7);
        int slots = (mode&1)?PS_IN_SLOTS:SBR_SLOTS;
        float *out = (mode&1)?NULL:output;
        float (*slot)[2] = (float (*)[2])candidate_frame;
        memcpy(saved,ch, sizeof(*saved));
        memcpy(pcm_ref,output, sizeof(float[1024]));
        float ref_slot[64][2];
        /* The reference retains all eight slots, including nonzero values in
         * the discarded prefix, to catch accidental reads of those slots. */
        float ref_tail[SBR_MAX_BANDS][SBR_T_HFGEN][2];
        for (int k = 0; k < SBR_MAX_BANDS; k++) {
            for (int n = 0; n < SBR_T_HFADJ; n++)
                ref_tail[k][n][0] = ref_tail[k][n][1] = 0.25f;
            memcpy(ref_tail[k] + SBR_T_HFADJ, ch->y_tail[k], sizeof(ch->y_tail[k]));
        }
        sbr_process_reference(&el,saved, &reference,pcm_ref,E,Q,hf,slots,
                              out ? pcm_ref : NULL, out ? ref_slot : (float (*)[2])reference_frame, ref_tail);
        float (*head)[SBR_T_HFGEN][2] = (float (*)[SBR_T_HFGEN][2])(dec->spec[0]+128);
        sbr_process_channel(&el,ch, &dec->sbr_scratch,output,E,Q,hf,slots,out,slot,head);
        assert(memcmp(ch,saved, sizeof(*ch)) == 0);
        if (out) assert(memcmp(output,pcm_ref, sizeof(float[2048])) == 0);
        else assert(memcmp(candidate_frame,reference_frame,
                            sizeof(float[PS_IN_SLOTS][64][2])) == 0);
        cases++;
        up+=hf && ch->t_E[0] > 0 && prev.kx < el.kx;
        down+=hf && ch->t_E[0] > 0 && prev.kx > el.kx;
    }
    fprintf(stderr,"tables = %d cases = %zu crossover-up = %zu down = %zu\n",nt,cases,up,down);
    assert(cases && up && down);
    free(reference.x_low);
    free(reference.y);
    free(reference_frame);
    free(candidate_frame);
    free(saved);
    free(pcm_ref);
    faad_decoder_close(&dec);
    return 0;
}
