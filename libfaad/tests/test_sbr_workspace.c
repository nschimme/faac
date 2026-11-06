/*
 * FAAD - Freeware Advanced Audio Decoder
 * Copyright (C) 2026 Nils Schimmelmann
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */

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
    initial.nch = 1;
    dec->sbr_el[0] = initial;
    memset(&dec->sbr[0], 0x35, sizeof(dec->sbr[0]));
    dec->sbr[0].have_frame = true;
    dec->sbr[0].el_ch = 0; /* the element's own channel: only the header resets it */
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

static void test_sbr_frequency_tables(void)
{
    static const uint32_t rates[] = { 22050, 32000, 44100, 48000, 96000 };
    static const uint8_t starts[] = { 0, 4, 8, 12, 15 };
    static const uint8_t stops[] = { 0, 4, 8, 12, 14, 15 };
    unsigned valid[4][4] = {{0}};

    for (unsigned r = 0; r < sizeof(rates) / sizeof(rates[0]); r++)
    for (unsigned scale = 0; scale <= 3; scale++)
    for (unsigned alter = 0; alter <= 1; alter++)
    for (unsigned limiter = 0; limiter <= 3; limiter++)
    for (unsigned si = 0; si < sizeof(starts) / sizeof(starts[0]); si++)
    for (unsigned ti = 0; ti < sizeof(stops) / sizeof(stops[0]); ti++)
    for (unsigned xover = 0; xover <= 4; xover++) {
        SBRElement el = {0};
        el.start_freq = starts[si];
        el.stop_freq = stops[ti];
        el.freq_scale = (uint8_t)scale;
        el.alter_scale = (uint8_t)alter;
        el.xover_band = (uint8_t)xover;
        el.noise_bands = 2;
        el.limiter_bands = (uint8_t)limiter;
        if (!sbr_build_tables(&el, rates[r])) continue;

        valid[scale][limiter]++;
        assert(el.n_master > 0 && el.n_high > 0 && el.n_low > 0);
        assert(el.f_master[0] == el.k0);
        assert(el.f_master[el.n_master] == el.k2);
        for (unsigned i = 1; i <= el.n_master; i++)
            assert(el.f_master[i] > el.f_master[i - 1]);
        for (unsigned i = 1; i <= el.n_low; i++)
            assert(el.f_low[i] > el.f_low[i - 1]);
        for (unsigned i = 1; i <= el.n_q; i++)
            assert(el.f_noise[i] >= el.f_noise[i - 1]);
        for (unsigned i = 1; i <= el.n_lim; i++)
            assert(el.f_lim[i] >= el.f_lim[i - 1]);
        assert(el.num_patches > 0 && el.num_patches <= SBR_MAX_PATCHES);
    }

    for (unsigned scale = 0; scale <= 3; scale++)
    for (unsigned limiter = 0; limiter <= 3; limiter++)
        assert(valid[scale][limiter] > 0);

    SBRElement el = {0};
    el.freq_scale = 2;
    el.noise_bands = 2;
    el.limiter_bands = 2;
    assert(!sbr_build_tables(&el, 7999));
    assert(!sbr_build_tables(&el, 192001));
}

int main(void)
{
    test_sbr_frequency_tables();
    test_truncated_history();
    test_valid_header_reset_then_invalid_grid();
#ifndef FAAD_DISABLE_PS
    check_hybrid_input();
#endif
    return 0;
}
