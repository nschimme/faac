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
        float (*slot)[2] = pcm ? output_slot : sc->x[i];
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
                                float E[SBR_MAX_ENV][SBR_MAX_BANDS], float Q[2][SBR_MAX_NQ], bool have_hf, int nslots, float *out_pcm, float (*output_slot)[2])
{
    sbr_analyse(ch, sc, pcm);

    /* Y carries the previous frame's tail; the HF generator then writes
     * the region this frame adjusts, the adjuster rewrites it in place
     * (each envelope's energy is read before its slots are scaled), and
     * the rest stays zero. */
    memset(sc->y, 0, sizeof(float[SBR_MAX_BANDS][SBR_BUF_SLOTS][2]));
    for (int k = 0; k < SBR_MAX_BANDS; k++)
        memcpy(sc->y[k], ch->y_tail[k], sizeof(ch->y_tail[k]));

    if (have_hf) {
        sbr_chirp(el, ch);
        sbr_hf_generate(el, ch, sc);
        sbr_hf_adjust(el, ch, sc, E, Q);
    }

    sbr_assemble_reference(el, ch, sc, have_hf, nslots, out_pcm, output_slot);

    for (int k = 0; k < SBR_MAX_BANDS; k++)
        memcpy(ch->y_tail[k], &sc->y[k][SBR_SLOTS], sizeof(ch->y_tail[k]));

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

int main(void)
{
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
    dec->sbr_scratch.x = calloc(PS_IN_SLOTS, sizeof(*dec->sbr_scratch.x));
    assert(dec->sbr_scratch.x);
    float *output = dec->pcm;
    SBRScratch reference = {0};
    reference.x_low = calloc(32, sizeof(*reference.x_low));
    reference.y = calloc(SBR_MAX_BANDS, sizeof(*reference.y));
    reference.x = calloc(PS_IN_SLOTS, sizeof(*reference.x));
    SBRChannel *saved = malloc(sizeof(*saved));
    float *pcm_ref = malloc(sizeof(float[2048]));
    assert(reference.x_low && reference.y && reference.x && saved && pcm_ref);
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
        for (int k = prev.kx;k < prev.kx+prev.M; k++) for (int n = 0;n < 8; n++) for (int z = 0;z < 2; z++) ch->y_tail[k][n][z] = sample();
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
        float (*slot)[2] = (float (*)[2])dec->spec[0];
        memcpy(saved,ch, sizeof(*saved));
        memcpy(pcm_ref,output, sizeof(float[1024]));
        float ref_slot[64][2];
        sbr_process_reference(&el,saved, &reference,pcm_ref,E,Q,hf,slots,
                              out ? pcm_ref : NULL, ref_slot);
        float (*head)[SBR_T_HFGEN][2] = (float (*)[SBR_T_HFGEN][2])(dec->spec[0]+128);
        sbr_process_channel(&el,ch, &dec->sbr_scratch,output,E,Q,hf,slots,out,slot,head);
        assert(memcmp(ch,saved, sizeof(*ch)) == 0);
        if (out) assert(memcmp(output,pcm_ref, sizeof(float[2048])) == 0);
        else assert(memcmp(dec->sbr_scratch.x,reference.x,
                            sizeof(float[PS_IN_SLOTS][64][2])) == 0);
        cases++;
        up+=hf && ch->t_E[0] > 0 && prev.kx < el.kx;
        down+=hf && ch->t_E[0] > 0 && prev.kx > el.kx;
    }
    fprintf(stderr,"tables = %d cases = %zu crossover-up = %zu down = %zu\n",nt,cases,up,down);
    assert(cases && up && down);
    free(reference.x_low);
    free(reference.y);
    free(reference.x);
    free(saved);
    free(pcm_ref);
    faad_decoder_close(&dec);
    return 0;
}
