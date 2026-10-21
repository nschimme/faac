/* SBR parse rollback and PS hybrid-input layout contracts. */
#include "sbr.c"
#undef NDEBUG
#include <assert.h>

#ifndef FAAD_DISABLE_PS

static uint32_t seed = 1234567;

static float sample(void)
{
    seed = seed * 1664525u + 1013904223u;
    return ((int)(seed >> 16) - 32768) / 32768.0f;
}

/* Layout rule: low band below kx, HF in [kx, kx + M), zero above;
 * look-ahead slots contain low band only. Previous-frame borders apply
 * to the leading tail slots, so crossover changes preserve their layout. */
static void assemble_band_layout(const SBRElement *el, SBRChannel *ch, SBRScratch *sc, bool have_hf, int nslots, float (*output_slot)[2])
{
    int i_temp = have_hf ? (int)ch->t_E_end_prev - SBR_SLOTS : 0;
    if (i_temp < 0) i_temp = 0;
    for (int i = 0; i < nslots; i++) {
        float (*slot)[2] = output_slot + i * 64;
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
    }
}

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
        assemble_band_layout(&el, &ch, &reference, hf, PS_IN_SLOTS,
                             (float (*)[2])frame);
        for (int i = 0; i < PS_IN_SLOTS; i++) {
            float input[6][2];
            for (int k = 0; k < 6; k++) input[k][0] = input[k][1] = 123.0f;
            sbr_assemble_hybrid_input(&el, &ch, &sc, hf, i, input, head, SBR_SLOTS);
            assert(memcmp(input, frame[i], sizeof(float[5][2])) == 0);
            assert(input[5][0] == 123.0f && input[5][1] == 123.0f);
        }
    }
}

#endif

static void put_bits(uint8_t *out, unsigned *pos, unsigned value, unsigned count)
{
    for (unsigned i = 0; i < count; i++) {
        unsigned bit = (value >> (count - 1 - i)) & 1u;
        out[*pos >> 3] |= (uint8_t)(bit << (7 - (*pos & 7)));
        ++*pos;
    }
}

static void test_valid_header_reset_then_invalid_grid(void)
{
    faad_config cfg;
    faad_config_init(&cfg, sizeof(cfg));
    faad_decoder *dec = NULL;
    assert(faad_decoder_open(&cfg, NULL, 0, &dec) == FAAD_OK);
    assert(faad_ensure_sbr(dec) == FAAD_OK);
    dec->core_sample_rate = 24000;
    dec->asc.sbr_sample_rate = 48000;

    SBRElement initial = {0}, changed = {0};
    initial.freq_scale = changed.freq_scale = 2;
    initial.alter_scale = changed.alter_scale = 1;
    initial.noise_bands = changed.noise_bands = 2;
    initial.limiter_bands = changed.limiter_bands = 2;
    initial.limiter_gains = changed.limiter_gains = 2;
    initial.interpol_freq = changed.interpol_freq = 1;
    initial.smoothing_mode = changed.smoothing_mode = 1;
    bool found = false;
    for (int a = 0; a < 16 && !found; a++)
    for (int b = 0; b < 16 && !found; b++) {
        initial.start_freq = (uint8_t)a;
        initial.stop_freq = (uint8_t)b;
        if (!sbr_build_tables(&initial, 48000)) continue;
        for (int c = 0; c < 16 && !found; c++)
        for (int d = 0; d < 16 && !found; d++) {
            if (c == a) continue;
            changed = initial;
            changed.start_freq = (uint8_t)c;
            changed.stop_freq = (uint8_t)d;
            if (sbr_build_tables(&changed, 48000)) found = true;
        }
    }
    assert(found);
    initial.header_present = true;
    dec->sbr_el[0] = initial;
    memset(&dec->sbr[0], 0x35, sizeof(dec->sbr[0]));
    dec->sbr[0].have_frame = true;
    dec->sbr[0].E_prev[0] = 123;
    dec->sbr[0].Q_prev[0] = 456;
    SBRChannel *saved_ch = malloc(sizeof(*saved_ch));
    SBRElement saved_el = dec->sbr_el[0];
    assert(saved_ch);
    memcpy(saved_ch, &dec->sbr[0], sizeof(*saved_ch));

    uint8_t payload[4] = {0};
    unsigned pos = 0;
    put_bits(payload, &pos, 1, 1); /* header_flag */
    put_bits(payload, &pos, 0, 1); /* amp_res */
    put_bits(payload, &pos, changed.start_freq, 4);
    put_bits(payload, &pos, changed.stop_freq, 4);
    put_bits(payload, &pos, 0, 3); /* xover_band */
    put_bits(payload, &pos, 0, 2); /* reserved */
    put_bits(payload, &pos, 0, 1); /* extra1 */
    put_bits(payload, &pos, 0, 1); /* extra2 */
    put_bits(payload, &pos, 0, 1); /* data_extra */
    put_bits(payload, &pos, 0, 2); /* FIXFIX */
    put_bits(payload, &pos, 3, 2); /* invalid 8-envelope count */
    assert(pos == 22);

    /* Direct parser call proves a valid header was applied and synthesis
     * reset was staged before the deliberately invalid grid was rejected. */
    BitReader direct;
    bits_init(&direct, payload, sizeof(payload));
    assert(sbr_read_extension(dec, &direct, 0, ID_SCE, false) == FAAD_ERR_DECODE_FAILED);
    assert(dec->sbr_el[0].start_freq == changed.start_freq);
    assert(dec->sbr_el[0].stop_freq == changed.stop_freq);
    assert(dec->sbr[0].E_prev[0] == 0 && dec->sbr[0].Q_prev[0] == 0);

    /* The public transactional wrapper must roll back the entire failed fill. */
    dec->sbr_el[0] = saved_el;
    memcpy(&dec->sbr[0], saved_ch, sizeof(*saved_ch));
    BitReader wrapped;
    bits_init(&wrapped, payload, sizeof(payload));
    assert(sbr_decode_extension(dec, &wrapped, 0, ID_SCE, false) == FAAD_ERR_DECODE_FAILED);
    assert(memcmp(saved_ch, &dec->sbr[0], sizeof(*saved_ch)) == 0);
    assert(memcmp(&saved_el, &dec->sbr_el[0], sizeof(saved_el)) == 0);
    free(saved_ch);
    assert(faad_decoder_close(&dec) == FAAD_OK);
}

static void test_truncated_history(void)
{
    faad_config cfg;
    faad_config_init(&cfg, sizeof(cfg));
    faad_decoder *dec;
    assert(faad_decoder_open(&cfg, NULL, 0, &dec) == FAAD_OK);
    assert(faad_ensure_sbr(dec) == FAAD_OK);
    SBRChannel *saved = malloc(sizeof(*saved));
    assert(saved);
    memset(&dec->sbr[0], 0x35, sizeof(*saved));
    memcpy(saved, &dec->sbr[0], sizeof(*saved));
    SBRElement el = dec->sbr_el[0];
    /* A header starts but cannot fit. No reset or parsed data may survive. */
    const uint8_t payload[] = {0x80};
    BitReader bs;
    bits_init(&bs, payload, sizeof(payload));
    assert(sbr_decode_extension(dec, &bs, 0, ID_SCE, false) == FAAD_ERR_DECODE_FAILED);
    assert(memcmp(saved, &dec->sbr[0], sizeof(*saved)) == 0);
    assert(memcmp(&el, &dec->sbr_el[0], sizeof(el)) == 0);
    free(saved);
    assert(faad_decoder_close(&dec) == FAAD_OK);
}

int main(void)
{
    test_truncated_history();
    test_valid_header_reset_then_invalid_grid();
#ifndef FAAD_DISABLE_PS
    check_hybrid_input();
#endif
    return 0;
}
