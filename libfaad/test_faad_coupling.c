/* Hand-built AAC-LC CCE packets. Compare their PCM with ordinary SCE/CPE
 * packets carrying the same spectrum, including a second overlap frame. */
#include "faad3.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
typedef struct { uint8_t data[2048]; unsigned bits; } Bits;
static void put(Bits *b, unsigned v, unsigned n)
{
    CHECK(b->bits + n <= sizeof(b->data) * 8);
    while (n--) {
        if (!(b->bits & 7)) b->data[b->bits / 8] = 0;
        b->data[b->bits / 8] |= (uint8_t)(((v >> n) & 1) << (7 - (b->bits & 7)));
        b->bits++;
    }
}
static void sf(Bits *b, int delta)
{
    if (delta == 0) put(b, 0, 1);
    else if (delta == -1) put(b, 4, 3);
    else if (delta == 1) put(b, 10, 4);
    else if (delta == 2) put(b, 12, 4);
    else CHECK(0);
}
/* Two bands; book 1's 10000 codeword reconstructs (1,0,0,0). At 48 kHz
 * each of the first two bands is four lines for both window sizes. */
static void ics(Bits *b, unsigned mask, unsigned seq, unsigned grouping, int tns)
{
    put(b, 140, 8); put(b, 0, 1); put(b, seq, 2); put(b, 0, 1);
    unsigned groups = 1, lengths[8] = {1};
    if (seq == 2) {
        put(b, 2, 4); put(b, grouping, 7);
        for (unsigned i = 0; i < 7; i++) {
            if (grouping & (1u << (6 - i))) lengths[groups - 1]++;
            else lengths[groups++] = 1;
        }
    } else { put(b, 2, 6); put(b, 0, 1); }
    for (unsigned g = 0; g < groups; g++)
        for (unsigned s = 0; s < 2; s++) {
            put(b, (mask >> s) & 1, 4); put(b, 1, seq == 2 ? 3 : 5);
        }
    for (unsigned g = 0; g < groups; g++)
        for (unsigned s = 0; s < 2; s++) if (mask & (1u << s)) sf(b, 0);
    put(b, 0, 1); put(b, tns != 0, 1);
    if (tns) for (unsigned w = 0; w < (seq == 2 ? 8u : 1u); w++) {
        put(b, 1, seq == 2 ? 1 : 2); put(b, 0, 1);
        put(b, seq == 2 ? 15 : 63, seq == 2 ? 4 : 6);
        put(b, 1, seq == 2 ? 3 : 5); put(b, 0, 1); put(b, 0, 1); put(b, 1, 3);
    }
    put(b, 0, 1);
    for (unsigned g = 0; g < groups; g++)
        for (unsigned s = 0; s < 2; s++) if (mask & (1u << s))
            for (unsigned w = 0; w < lengths[g]; w++) put(b, 16, 5);
}
static void channel(Bits *b, int pair, unsigned left, unsigned right, unsigned seq, unsigned grouping, int tns)
{
    put(b, pair ? 1 : 0, 3); put(b, 5, 4);
    if (pair) put(b, 0, 1); /* separate windows */
    ics(b, left, seq, grouping, tns);
    if (pair) ics(b, right, seq, grouping, tns);
}
static void cce(Bits *b, unsigned tag, unsigned point, int pair, unsigned select,
                unsigned mask, unsigned seq, unsigned grouping, int gain, int common,
                int sign, unsigned scale, int missing, int source_tns)
{
    put(b, 2, 3); put(b, tag, 4); put(b, point == 2, 1);
    /* Two targets give a transmitted gain list even for mono. First target
     * has unit gain; the second duplicates it to exercise gain decoding. */
    put(b, pair && select == 3 ? 0 : 1, 3);
    unsigned targets = pair && select == 3 ? 1 : 2;
    for (unsigned t = 0; t < targets; t++) {
        put(b, pair, 1); put(b, missing ? 7 : 5, 4);
        if (pair) put(b, select, 2);
    }
    put(b, point == 1, 1); put(b, sign, 1); put(b, scale, 2);
    ics(b, mask, seq, grouping, source_tns);
    if (point != 2) put(b, common, 1);
    if (point == 2 || common) sf(b, gain);
    else {
        unsigned groups = 1;
        if (seq == 2) for (unsigned i = 0; i < 7; i++) groups += !(grouping & (1u << i));
        for (unsigned g = 0; g < groups; g++)
            for (unsigned s = 0; s < 2; s++) if (mask & (1u << s)) sf(b, g == 0 && s == 0 ? gain : 0);
    }
}
static faad_decoder *open_decoder(int pair, int small)
{
    faad_config cfg;
    CHECK(faad_config_init(&cfg, sizeof cfg) == FAAD_OK);
    cfg.stream_format = FAAD_STREAM_RAW; cfg.output_format = FAAD_OUTPUT_FLOAT;
    uint8_t asc[] = {0x11, pair ? 0x90 : 0x88};
    if (small) asc[1] |= 4;
    faad_decoder *dec = NULL;
    CHECK(faad_decoder_open(&cfg, asc, sizeof asc, &dec) == FAAD_OK);
    return dec;
}
static unsigned decode(faad_decoder *dec, Bits *b, float *pcm, int concealed)
{
    uint32_t used, wrote, flags;
    faad_stream_info si = {.struct_size = sizeof si};
    CHECK(faad_decoder_get_info(dec, &si) == FAAD_OK);
    CHECK(si.max_output_bytes <= 65536);
    unsigned len = (b->bits + 7) / 8;
    CHECK(faad_decode_frame(dec, b->data, len, &used, pcm, si.max_output_bytes, &wrote, &flags) == FAAD_OK);
    CHECK(used == len);
    CHECK(!!(flags & FAAD_FRAME_CONCEALED) == concealed);
    return wrote / sizeof(float);
}
static void equal(const float *a, const float *b, unsigned n, float factor)
{
    float energy = 0;
    for (unsigned i = 0; i < n; i++) {
        CHECK(isfinite(a[i])); CHECK(fabsf(a[i] - b[i] * factor) < 0.000002f);
        energy += b[i] * b[i];
    }
    CHECK(energy > 0.000000001f);
}
static void run(unsigned point, int pair, unsigned select, unsigned seq, unsigned grouping,
                int before, int gain, int common, int sign, unsigned scale, int small)
{
    Bits b = {{0},0}, ref = {{0},0};
    unsigned source_seq = seq, target_seq = point == 2 ? 0 : seq;
    if (!before) channel(&b, pair, 0, 0, target_seq, grouping, point != 2);
    cce(&b, 9, point, pair, select, 3, source_seq, grouping, gain, common, sign, scale, 0, 0);
    if (before) channel(&b, pair, 0, 0, target_seq, grouping, point != 2);
    put(&b, 7, 3);
    channel(&ref, pair, select == 1 ? 0 : 3, select == 2 ? 0 : 3, source_seq, grouping, point == 0);
    put(&ref, 7, 3);
    faad_decoder *dec = open_decoder(pair, small), *control = open_decoder(pair, small);
    float pcm[16384], expected[16384];
    float factor = 1 + exp2f(-gain * (float)(1u << scale) / 8);
    if (!common && sign) factor = 1 + ((gain & 1) ? -1 : 1) * exp2f(-((gain - (gain & 1)) / 2) * (float)(1u << scale) / 8);
    for (unsigned f = 0; f < 3; f++) {
        unsigned n = decode(dec, &b, pcm, 0);
        CHECK(decode(control, &ref, expected, 0) == n);
        if (pair && select == 3) {
            for (unsigned i = 0; i < n; i++) {
                float k = i & 1 ? factor - 1 : 1;
                CHECK(fabsf(pcm[i] - k * expected[i]) < 0.000002f);
            }
        } else equal(pcm, expected, n, factor);
    }
    CHECK(faad_decoder_flush(dec) == FAAD_OK); CHECK(faad_decoder_flush(control) == FAAD_OK);
    unsigned n = decode(dec, &b, pcm, 0); CHECK(decode(control, &ref, expected, 0) == n);
    if (!(pair && select == 3)) equal(pcm, expected, n, factor);
    CHECK(faad_decoder_close(&dec) == FAAD_OK); CHECK(faad_decoder_close(&control) == FAAD_OK);
}
static void multiple_and_recovery(void)
{
    Bits b = {{0},0}, ref = {{0},0}, silence = {{0},0};
    channel(&b, 0, 0, 0, 0, 0, 0);
    cce(&b, 1, 0, 0, 0, 3, 0, 0, 0, 1, 0, 0, 0, 0);
    cce(&b, 2, 1, 0, 0, 3, 0, 0, 0, 1, 0, 0, 0, 1);
    cce(&b, 3, 2, 0, 0, 3, 0, 0, 0, 1, 0, 0, 0, 0);
    put(&b, 7, 3);
    channel(&ref, 0, 3, 0, 0, 0, 0); put(&ref, 7, 3);
    channel(&silence, 0, 0, 0, 0, 0, 0); put(&silence, 7, 3);
    faad_decoder *dec = open_decoder(0, 0), *control = open_decoder(0, 0);
    float pcm[16384], expected[16384];
    /* A separate TNS-bearing source verifies the source's own filter. */
    Bits filtered = {{0},0};
    channel(&filtered, 0, 3, 0, 0, 0, 1); put(&filtered, 7, 3);
    faad_decoder *tns = open_decoder(0, 0);
    float filtered_pcm[16384];
    for (unsigned f = 0; f < 3; f++) {
        unsigned n = decode(dec, &b, pcm, 0);
        CHECK(decode(control, &ref, expected, 0) == n);
        CHECK(decode(tns, &filtered, filtered_pcm, 0) == n);
        for (unsigned i = 0; i < n; i++) CHECK(fabsf(pcm[i] - 4 * expected[i] - 2 * filtered_pcm[i]) < 0.000002f);
    }
    /* Truncate at every byte, flush, and verify the next complete CCE frame.
     * Failed packets must neither retain gains nor poison overlap history. */
    unsigned full_bits = b.bits;
    for (unsigned bytes = 1; bytes < (full_bits + 7) / 8; bytes++) {
        CHECK(faad_decoder_flush(dec) == FAAD_OK);
        b.bits = bytes * 8; decode(dec, &b, pcm, 1); b.bits = full_bits;
        CHECK(faad_decoder_flush(dec) == FAAD_OK);
        CHECK(faad_decoder_flush(control) == FAAD_OK); CHECK(faad_decoder_flush(tns) == FAAD_OK);
        unsigned n = decode(dec, &b, pcm, 0);
        decode(control, &ref, expected, 0); decode(tns, &filtered, filtered_pcm, 0);
        for (unsigned i = 0; i < n; i++) CHECK(fabsf(pcm[i] - 4 * expected[i] - 2 * filtered_pcm[i]) < 0.000002f);
    }
    CHECK(faad_decoder_flush(dec) == FAAD_OK);
    unsigned n = decode(dec, &silence, pcm, 0);
    for (unsigned i = 0; i < n; i++) CHECK(pcm[i] == 0);
    CHECK(faad_decoder_close(&dec) == FAAD_OK); CHECK(faad_decoder_close(&control) == FAAD_OK);
    CHECK(faad_decoder_close(&tns) == FAAD_OK);
}

#if MAX_CHANNELS >= 6
static void six_channels(Bits *b, unsigned mask)
{
    put(b, 0, 3); put(b, 5, 4); ics(b, mask, 0, 0, 0);
    for (unsigned tag = 2; tag <= 3; tag++) {
        put(b, 1, 3); put(b, tag, 4); put(b, 0, 1);
        ics(b, mask, 0, 0, 0); ics(b, mask, 0, 0, 0);
    }
    put(b, 3, 3); put(b, 4, 4); ics(b, 0, 0, 0, 0);
}
static void surround(void)
{
    for (unsigned point = 0; point < 3; point++)
        for (unsigned mode = 0; mode < 3; mode++) {
            Bits b = {{0},0}, ref = {{0},0};
            six_channels(&b, 0);
            put(&b, 2, 3); put(&b, 9, 4); put(&b, point == 2, 1); put(&b, 2, 3);
            put(&b, 0, 1); put(&b, 5, 4); /* SCE */
            for (unsigned tag = 2; tag <= 3; tag++) {
                put(&b, 1, 1); put(&b, tag, 4); put(&b, 0, 2); /* shared stereo gain */
            }
            put(&b, point == 1, 1); put(&b, 0, 1); put(&b, 0, 2);
            ics(&b, 3, 0, 0, 0);
            for (unsigned l = 1; l < 3; l++) { if (point != 2) put(&b, 1, 1); sf(&b, 0); }
            put(&b, 7, 3);
            six_channels(&ref, 3); put(&ref, 7, 3);
            faad_config cfg; CHECK(faad_config_init(&cfg, sizeof cfg) == FAAD_OK);
            cfg.stream_format = FAAD_STREAM_RAW; cfg.output_format = FAAD_OUTPUT_FLOAT;
            cfg.downmix_mode = mode;
            const uint8_t asc[] = {0x11, 0xb0};
            faad_decoder *dec, *control;
            CHECK(faad_decoder_open(&cfg, asc, sizeof asc, &dec) == FAAD_OK);
            CHECK(faad_decoder_open(&cfg, asc, sizeof asc, &control) == FAAD_OK);
            float pcm[16384], expected[16384];
            for (unsigned f = 0; f < 3; f++) {
                unsigned n = decode(dec, &b, pcm, 0);
                CHECK(decode(control, &ref, expected, 0) == n);
                equal(pcm, expected, n, 1);
            }
            CHECK(faad_decoder_close(&dec) == FAAD_OK); CHECK(faad_decoder_close(&control) == FAAD_OK);
        }
}
#endif

static void malformed(void)
{
    for (int kind = 0; kind < 4; kind++) {
        Bits b = {{0},0}; channel(&b, 0, 0, 0, 0, 0, 0);
        cce(&b, 9, 0, 0, 0, 3, kind == 3 ? 2 : 0, 0, 1, 1, 0, 3, kind == 1, 0);
        if (kind == 2) cce(&b, 9, 0, 0, 0, 3, 0, 0, 1, 1, 0, 3, 0, 0);
        put(&b, 7, 3);
        if (kind == 0) b.bits -= 8; /* truncated gain/END */
        faad_decoder *dec = open_decoder(0, 0); float pcm[16384];
        decode(dec, &b, pcm, 1); CHECK(faad_decoder_close(&dec) == FAAD_OK);
    }
}
int main(void)
{
    for (unsigned point = 0; point < 3; point++)
        for (int before = 0; before < 2; before++)
            for (unsigned scale = 0; scale < 4; scale++) {
                run(point, 0, 0, 0, 0, before, 1, 1, 0, scale, 0);
                run(point, 0, 0, 2, 0x55, before, 0, 1, 0, scale, 0);
                if (point != 2) {
                    run(point, 0, 0, 2, 0x7f, before, 1, 0, 1, scale, 0);
                    run(point, 0, 0, 0, 0, before, -1, 0, 1, scale, 0);
                    run(point, 0, 0, 0, 0, before, 2, 0, 1, scale, 0);
                    run(point, 0, 0, 0, 0, before, -1, 1, 0, scale, 0);
                }
#if MAX_CHANNELS >= 2
                for (unsigned select = 0; select < 4; select++)
                    run(point, 1, select, 0, 0, before, 1, 1, 0, scale, 0);
#endif
            }
#ifndef FAAD_DISABLE_DAB
    for (unsigned point = 0; point < 3; point++) run(point, 0, 0, 0, 0, 1, 0, 1, 0, 0, 1);
#endif
    malformed(); multiple_and_recovery();
#if MAX_CHANNELS >= 6
    surround();
#endif
    puts("AAC coupling: PASS"); return 0;
}
