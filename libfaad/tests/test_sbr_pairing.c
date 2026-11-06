/*
 * FAAD - Freeware Advanced Audio Decoder
 * Copyright (C) 2026 Mar Dominguez
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

/* SBR channel ownership across element layout changes: sbr_apply pairs and
 * enhances only the channels whose payload came from the element it runs,
 * while that element is still in the frame's layout. */
#include "sbr.c"
#undef NDEBUG
#include <assert.h>

static void put_bits(uint8_t *out, unsigned *pos, unsigned value, unsigned count)
{
    for (unsigned i = 0; i < count; i++) {
        unsigned bit = (value >> (count - 1 - i)) & 1u;
        out[*pos >> 3] |= (uint8_t)(bit << (7 - (*pos & 7)));
        ++*pos;
    }
}

/* The codeword that decodes to a zero delta in one of the SBR books. */
static void put_zero(uint8_t *out, unsigned *pos, int book)
{
    for (unsigned len = 1; len <= 16; len++)
        for (uint32_t code = 0; code < (1u << len); code++) {
            uint8_t buf[4] = {0};
            unsigned p = 0;
            put_bits(buf, &p, code, len);
            BitReader bs;
            bits_init(&bs, buf, sizeof(buf));
            if (sbr_huff_decode(&bs, &sbr_books[book]) == 0 && bits_get_consumed(&bs) == len) {
                put_bits(out, pos, code, len);
                return;
            }
        }
    assert(!"no zero codeword");
}

static void put_header(uint8_t *out, unsigned *pos, const SBRElement *el)
{
    put_bits(out, pos, 1, 1); /* header_flag */
    put_bits(out, pos, 0, 1); /* amp_res */
    put_bits(out, pos, el->start_freq, 4);
    put_bits(out, pos, el->stop_freq, 4);
    put_bits(out, pos, 0, 3); /* xover_band */
    put_bits(out, pos, 0, 2); /* reserved */
    put_bits(out, pos, 0, 1); /* extra1 */
    put_bits(out, pos, 0, 1); /* extra2 */
}

/* A flat envelope; a balance of 12 (24 once doubled) is centred. */
static void put_envelope(uint8_t *out, unsigned *pos, int nb, bool balance)
{
    put_bits(out, pos, balance ? 12 : 30, balance ? 6 : 7);
    for (int k = 1; k < nb; k++) put_zero(out, pos, balance ? HB_F_ENV_BAL_15 : HB_F_ENV_15);
}

static void put_noise(uint8_t *out, unsigned *pos, int n_q, bool balance)
{
    put_bits(out, pos, balance ? 6 : 8, 5);
    for (int k = 1; k < n_q; k++) put_zero(out, pos, balance ? HB_F_ENV_BAL_30 : HB_F_ENV_30);
}

/* A coupled pair: FIXFIX, one low-resolution envelope. */
static unsigned cpe_coupled(uint8_t *out, const SBRElement *el)
{
    unsigned pos = 0;
    put_header(out, &pos, el);
    put_bits(out, &pos, 0, 1); /* data_extra */
    put_bits(out, &pos, 1, 1); /* coupling */
    put_bits(out, &pos, 0, 2); /* FIXFIX */
    put_bits(out, &pos, 0, 2); /* one envelope */
    put_bits(out, &pos, 0, 1); /* freq_res low */
    for (int c = 0; c < 2; c++) {
        put_bits(out, &pos, 0, 1); /* df_env */
        put_bits(out, &pos, 0, 1); /* df_noise */
    }
    for (int k = 0; k < el->n_q; k++) put_bits(out, &pos, 0, 2); /* invf_mode */
    put_envelope(out, &pos, el->n_low, false);
    put_noise(out, &pos, el->n_q, false);
    put_envelope(out, &pos, el->n_low, true);
    put_noise(out, &pos, el->n_q, true);
    put_bits(out, &pos, 0, 2); /* add_harmonic_flag, both channels */
    put_bits(out, &pos, 0, 1); /* extended_data */
    return (pos + 7) / 8;
}

/* A single channel: FIXFIX, four high-resolution envelopes. */
static unsigned sce_four(uint8_t *out, const SBRElement *el)
{
    unsigned pos = 0;
    put_header(out, &pos, el);
    put_bits(out, &pos, 0, 1); /* data_extra */
    put_bits(out, &pos, 0, 2); /* FIXFIX */
    put_bits(out, &pos, 2, 2); /* four envelopes */
    put_bits(out, &pos, 1, 1); /* freq_res high */
    put_bits(out, &pos, 0, 4); /* df_env */
    put_bits(out, &pos, 0, 2); /* df_noise */
    for (int k = 0; k < el->n_q; k++) put_bits(out, &pos, 0, 2); /* invf_mode */
    for (int l = 0; l < 4; l++) put_envelope(out, &pos, el->n_high, false);
    for (int l = 0; l < 2; l++) put_noise(out, &pos, el->n_q, false);
    put_bits(out, &pos, 0, 1); /* add_harmonic_flag */
    put_bits(out, &pos, 0, 1); /* extended_data */
    return (pos + 7) / 8;
}

static struct {
    SBRElement el;
    uint8_t cpe[256], sce[512];
    unsigned cpe_len, sce_len;
} fills;

/* A header whose grids differ between resolutions, and the fills built on it. */
static void build_fills(void)
{
    SBRElement *el = &fills.el;
    el->freq_scale = 2;
    el->alter_scale = 1;
    el->noise_bands = 2;
    el->limiter_bands = 2;
    for (int a = 0; a < 16; a++)
    for (int b = 0; b < 16; b++) {
        el->start_freq = (uint8_t)a;
        el->stop_freq = (uint8_t)b;
        if (sbr_build_tables(el, 48000) && el->n_high > el->n_low && el->n_q > 1) {
            fills.cpe_len = cpe_coupled(fills.cpe, el);
            fills.sce_len = sce_four(fills.sce, el);
            return;
        }
    }
    assert(!"no header");
}

static faad_decoder *open_sbr(void)
{
    faad_config cfg;
    faad_config_init(&cfg, sizeof(cfg));
    faad_decoder *dec = NULL;
    assert(faad_decoder_open(&cfg, NULL, 0, &dec) == FAAD_OK);
    assert(faad_ensure_sbr(dec) == FAAD_OK);
    dec->core_sample_rate = 24000;
    dec->asc.sbr_sample_rate = 48000;
    dec->num_channels = 2;
    return dec;
}

static void fill(faad_decoder *dec, uint32_t ch0, uint32_t syntax_id)
{
    BitReader bs;
    if (syntax_id == ID_CPE) bits_init(&bs, fills.cpe, fills.cpe_len);
    else bits_init(&bs, fills.sce, fills.sce_len);
    assert(sbr_decode_extension(dec, &bs, ch0, syntax_id, false) == FAAD_OK);
}

/* The syntax ids of a frame's channel elements, in channel order. */
typedef struct { uint8_t id[3]; uint32_t n; } Elems;
#define ELEMS_INIT(...) { { __VA_ARGS__ }, sizeof((uint8_t[]){ __VA_ARGS__ }) }
#define ELEMS(...) ((Elems)ELEMS_INIT(__VA_ARGS__))

/* A frame of channel elements, laid out as the core decode notes them, with
 * the same core samples in every channel, depending only on the frame. */
static void run_frame(faad_decoder *dec, int frame, Elems elements)
{
    uint32_t num_ch = 0;
    memset(dec->elem_nch, 0, sizeof(dec->elem_nch));
    for (uint32_t i = 0; i < elements.n; i++) {
        uint8_t nch = elements.id[i] == ID_CPE ? 2 : 1;
        assert(num_ch + nch <= MAX_CHANNELS);
        dec->elem_nch[num_ch] = nch;
        num_ch += nch;
    }
    uint32_t seed = 1234567u + (uint32_t)frame;
    for (int i = 0; i < 1024; i++) {
        seed = seed * 1664525u + 1013904223u;
        for (uint32_t c = 0; c < num_ch; c++) dec->pcm[c * SBR_OUT_LEN + i] = ((int)(seed >> 16) - 32768) / 8.0f;
    }
    sbr_apply(dec, num_ch, dec->pcm);
}

/* A CPE with coupled SBR, then two SCEs of which only the second carries
 * SBR. sbr_apply must not pair channel 1 under channel 0's stale coupled
 * element: channel 1 decodes as an SCE that never belonged to the CPE. */
static void test_sbr_apply_layout_change_unpairs(void)
{
    faad_decoder *changed = open_sbr(), *reference = open_sbr();
    int frame = 0;
    for (; frame < 2; frame++) {
        fill(changed, 0, ID_CPE);
        run_frame(changed, frame, ELEMS(ID_CPE));
        run_frame(reference, frame, ELEMS(ID_CPE));
    }
    for (; frame < 4; frame++) {
        fill(changed, 1, ID_SCE);
        fill(reference, 1, ID_SCE);
        run_frame(changed, frame, ELEMS(ID_SCE, ID_SCE));
        run_frame(reference, frame, ELEMS(ID_SCE, ID_SCE));
    }
    /* By the last frame, channel 1's QMF history holds only SCE frames. */
    assert(changed->sbr[1].primed && reference->sbr[1].primed);
    assert(memcmp(changed->pcm + SBR_OUT_LEN, reference->pcm + SBR_OUT_LEN,
                  sizeof(float[SBR_OUT_LEN])) == 0);
    assert(faad_decoder_close(&changed) == FAAD_OK);
    assert(faad_decoder_close(&reference) == FAAD_OK);
}

/* A CPE with coupled SBR, then an SCE at channel 0 that carries none and an
 * SCE at channel 1 that does. Channel 0 has no payload of its own, so it
 * decodes like a channel whose SBR never started. */
static void test_sbr_apply_stale_partner_not_enhanced(void)
{
    faad_decoder *changed = open_sbr(), *reference = open_sbr();
    int frame = 0;
    for (; frame < 2; frame++) {
        fill(changed, 0, ID_CPE);
        run_frame(changed, frame, ELEMS(ID_CPE));
        run_frame(reference, frame, ELEMS(ID_CPE));
    }
    for (; frame < 4; frame++) {
        fill(changed, 1, ID_SCE);
        fill(reference, 1, ID_SCE);
        run_frame(changed, frame, ELEMS(ID_SCE, ID_SCE));
        run_frame(reference, frame, ELEMS(ID_SCE, ID_SCE));
    }
    assert(memcmp(changed->pcm, reference->pcm, sizeof(float[SBR_OUT_LEN])) == 0);
    assert(faad_decoder_close(&changed) == FAAD_OK);
    assert(faad_decoder_close(&reference) == FAAD_OK);
}

/* A CPE with coupled SBR, then an SCE at channel 0 that carries none, alone
 * or beside a second SCE without SBR; and two SCEs with SBR, then a CPE in
 * their place that carries none. No fill marks the change, but the new
 * element has no payload of its own: its channels decode like channels
 * whose SBR never started, not with the old element's last payload. */
static void test_sbr_apply_unfilled_element_not_enhanced(void)
{
    static const Elems before[] = { ELEMS_INIT(ID_CPE), ELEMS_INIT(ID_CPE), ELEMS_INIT(ID_SCE, ID_SCE) };
    static const Elems after[] = { ELEMS_INIT(ID_SCE), ELEMS_INIT(ID_SCE, ID_SCE), ELEMS_INIT(ID_CPE) };
    for (int i = 0; i < 3; i++) {
        faad_decoder *changed = open_sbr(), *reference = open_sbr();
        int frame = 0;
        for (; frame < 2; frame++) {
            if (before[i].id[0] == ID_CPE) {
                fill(changed, 0, ID_CPE);
            } else {
                fill(changed, 0, ID_SCE);
                fill(changed, 1, ID_SCE);
            }
            run_frame(changed, frame, before[i]);
            run_frame(reference, frame, before[i]);
        }
        for (; frame < 4; frame++) {
            run_frame(changed, frame, after[i]);
            run_frame(reference, frame, after[i]);
        }
        size_t num_ch = i == 0 ? 1 : 2;
        assert(memcmp(changed->pcm, reference->pcm, num_ch * sizeof(float[SBR_OUT_LEN])) == 0);
        assert(faad_decoder_close(&changed) == FAAD_OK);
        assert(faad_decoder_close(&reference) == FAAD_OK);
    }
}

/* A CPE with coupled SBR, then two SCEs of which only the second carries
 * SBR, then the CPE again without a fill. Its element matches channel 0's
 * last fill, but channel 1 now holds the SCE's payload, so the pair has no
 * payload of its own and decodes like channels whose SBR never started. */
static void test_sbr_apply_returning_pair_not_enhanced(void)
{
    faad_decoder *changed = open_sbr(), *reference = open_sbr();
    int frame = 0;
    for (; frame < 2; frame++) {
        fill(changed, 0, ID_CPE);
        run_frame(changed, frame, ELEMS(ID_CPE));
        run_frame(reference, frame, ELEMS(ID_CPE));
    }
    for (; frame < 4; frame++) {
        fill(changed, 1, ID_SCE);
        run_frame(changed, frame, ELEMS(ID_SCE, ID_SCE));
        run_frame(reference, frame, ELEMS(ID_SCE, ID_SCE));
    }
    for (; frame < 6; frame++) {
        run_frame(changed, frame, ELEMS(ID_CPE));
        run_frame(reference, frame, ELEMS(ID_CPE));
    }
    assert(memcmp(changed->pcm, reference->pcm, sizeof(float[2 * SBR_OUT_LEN])) == 0);
    assert(faad_decoder_close(&changed) == FAAD_OK);
    assert(faad_decoder_close(&reference) == FAAD_OK);
}

/* An element that skips its fill in a frame continues its own last payload:
 * a CPE at channel 0, and a CPE after an SCE. */
static void test_sbr_apply_skipped_fill_continues(void)
{
    static const Elems layouts[] = { ELEMS_INIT(ID_CPE), ELEMS_INIT(ID_SCE, ID_CPE) };
    for (int i = 0; i < (MAX_CHANNELS >= 3 ? 2 : 1); i++) {
        uint32_t cpe = i, num_ch = i + 2;
        faad_decoder *skipping = open_sbr(), *refilled = open_sbr(), *reference = open_sbr();
        for (int frame = 0; frame < 4; frame++) {
            if (frame < 2) {
                if (cpe) fill(skipping, 0, ID_SCE);
                fill(skipping, cpe, ID_CPE);
            }
            if (cpe) fill(refilled, 0, ID_SCE);
            fill(refilled, cpe, ID_CPE);
            run_frame(skipping, frame, layouts[i]);
            run_frame(refilled, frame, layouts[i]);
            run_frame(reference, frame, layouts[i]);
        }
        assert(memcmp(skipping->pcm, refilled->pcm, num_ch * sizeof(float[SBR_OUT_LEN])) == 0);
        for (uint32_t c = 0; c < num_ch; c++)
            assert(memcmp(skipping->pcm + c * SBR_OUT_LEN, reference->pcm + c * SBR_OUT_LEN,
                          sizeof(float[SBR_OUT_LEN])) != 0);
        assert(faad_decoder_close(&skipping) == FAAD_OK);
        assert(faad_decoder_close(&refilled) == FAAD_OK);
        assert(faad_decoder_close(&reference) == FAAD_OK);
    }
}

#if MAX_CHANNELS >= 3
/* An SCE at channel 0 and a CPE at channel 1, then an SCE at channel 0, an
 * SCE at channel 1 without SBR and an SCE at channel 2. Channel 1 has no
 * payload of its own and decodes like a channel whose SBR never started,
 * and channel 2 like an SCE that never belonged to the CPE. */
static void test_sbr_apply_layout_change_beyond_first_pair(void)
{
    faad_decoder *changed = open_sbr(), *reference = open_sbr();
    int frame = 0;
    for (; frame < 2; frame++) {
        fill(changed, 0, ID_SCE);
        fill(changed, 1, ID_CPE);
        fill(reference, 0, ID_SCE);
        run_frame(changed, frame, ELEMS(ID_SCE, ID_CPE));
        run_frame(reference, frame, ELEMS(ID_SCE, ID_CPE));
    }
    for (; frame < 4; frame++) {
        fill(changed, 0, ID_SCE);
        fill(changed, 2, ID_SCE);
        fill(reference, 0, ID_SCE);
        fill(reference, 2, ID_SCE);
        run_frame(changed, frame, ELEMS(ID_SCE, ID_SCE, ID_SCE));
        run_frame(reference, frame, ELEMS(ID_SCE, ID_SCE, ID_SCE));
    }
    assert(memcmp(changed->pcm + SBR_OUT_LEN, reference->pcm + SBR_OUT_LEN,
                  sizeof(float[SBR_OUT_LEN])) == 0);
    assert(memcmp(changed->pcm + 2 * SBR_OUT_LEN, reference->pcm + 2 * SBR_OUT_LEN,
                  sizeof(float[SBR_OUT_LEN])) == 0);
    assert(faad_decoder_close(&changed) == FAAD_OK);
    assert(faad_decoder_close(&reference) == FAAD_OK);

    /* The CPE at channel 1 becomes an SCE and then a CPE again. */
    faad_decoder *dec = open_sbr();
    for (frame = 0; frame < 2; frame++) {
        fill(dec, 1, ID_CPE);
        run_frame(dec, frame, ELEMS(ID_SCE, ID_CPE));
    }
    assert(dec->sbr[1].primed && dec->sbr[1].E_prev[0] != 0);
    fill(dec, 1, ID_SCE);
    assert(!dec->sbr[1].primed && dec->sbr[1].E_prev[0] == 0);
    run_frame(dec, frame++, ELEMS(ID_SCE, ID_SCE, ID_SCE));
    fill(dec, 1, ID_CPE);
    assert(!dec->sbr[1].primed && !dec->sbr[2].primed && dec->sbr[2].E_prev[0] == 0);
    assert(faad_decoder_close(&dec) == FAAD_OK);
}
#endif

/* A CPE refilled every frame stays coupled: with a centred balance and the
 * same core samples, both channels decode alike. */
static void test_sbr_apply_coupled_pair_kept(void)
{
    faad_decoder *dec = open_sbr();
    for (int frame = 0; frame < 4; frame++) {
        fill(dec, 0, ID_CPE);
        run_frame(dec, frame, ELEMS(ID_CPE));
    }
    assert(dec->sbr[0].primed && dec->sbr[1].primed);
    assert(memcmp(dec->pcm, dec->pcm + SBR_OUT_LEN, sizeof(float[SBR_OUT_LEN])) == 0);
    assert(faad_decoder_close(&dec) == FAAD_OK);
}

/* A fill whose header leaves the tables alone continues only the channels
 * this element's previous fill included; any other channel starts over, as
 * its deltas and history belong to another element. */
static void test_sbr_read_extension_restarts_taken_channels(void)
{
    /* Channel 1 moves from an SCE of its own into a CPE at channel 0 and back. */
    faad_decoder *dec = open_sbr();
    int frame = 0;
    for (; frame < 2; frame++) {
        fill(dec, 1, ID_SCE);
        run_frame(dec, frame, ELEMS(ID_SCE, ID_SCE));
    }
    for (; frame < 4; frame++) {
        fill(dec, 0, ID_CPE);
        run_frame(dec, frame, ELEMS(ID_CPE));
    }
    assert(dec->sbr[1].primed && dec->sbr[1].E_prev[0] != 0);
    fill(dec, 1, ID_SCE);
    assert(!dec->sbr[1].primed && dec->sbr[1].E_prev[0] == 0);
    run_frame(dec, frame, ELEMS(ID_SCE, ID_SCE));
    assert(faad_decoder_close(&dec) == FAAD_OK);

    /* The CPE at channel 0 becomes an SCE and then a CPE again. */
    dec = open_sbr();
    for (frame = 0; frame < 2; frame++) {
        fill(dec, 0, ID_CPE);
        run_frame(dec, frame, ELEMS(ID_CPE));
    }
    assert(dec->sbr[0].primed && dec->sbr[0].E_prev[0] != 0);
    fill(dec, 0, ID_SCE);
    assert(!dec->sbr[0].primed && dec->sbr[0].E_prev[0] == 0);
    run_frame(dec, frame++, ELEMS(ID_SCE, ID_SCE));
    assert(dec->sbr[0].primed && dec->sbr[1].primed && dec->sbr[1].E_prev[0] != 0);
    fill(dec, 0, ID_CPE);
    assert(!dec->sbr[0].primed);
    assert(!dec->sbr[1].primed && dec->sbr[1].E_prev[0] == 0);
    run_frame(dec, frame, ELEMS(ID_CPE));
    assert(faad_decoder_close(&dec) == FAAD_OK);
}

int main(void)
{
    build_fills();
    test_sbr_apply_layout_change_unpairs();
    test_sbr_apply_stale_partner_not_enhanced();
    test_sbr_apply_unfilled_element_not_enhanced();
    test_sbr_apply_returning_pair_not_enhanced();
    test_sbr_apply_skipped_fill_continues();
#if MAX_CHANNELS >= 3
    test_sbr_apply_layout_change_beyond_first_pair();
#endif
    test_sbr_apply_coupled_pair_kept();
    test_sbr_read_extension_restarts_taken_channels();
    return 0;
}
