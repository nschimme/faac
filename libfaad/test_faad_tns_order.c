/* A hand-built AAC-LC frame whose only content is a TNS filter: the highest
 * long-block order LC allows decodes, one more is not a valid stream.
 * ISO/IEC 14496-3 syntax: single_channel_element, ics_info, section_data,
 * tns_data. The spectrum is all zero (codebook 0), so only the parse matters. */
#include <stdio.h>
#include <string.h>
#include "faad3.h"

typedef struct { uint8_t buf[64]; unsigned bits; } Bits;

static void put(Bits *b, unsigned value, unsigned width)
{
    while (width--) {
        if (b->bits % 8 == 0) b->buf[b->bits / 8] = 0;
        b->buf[b->bits / 8] |= (uint8_t)(((value >> width) & 1u) << (7 - b->bits % 8));
        b->bits++;
    }
}

static unsigned frame(Bits *b, unsigned order)
{
    memset(b, 0, sizeof *b);
    put(b, 0, 3);        /* ID_SCE */
    put(b, 0, 4);        /* element_instance_tag */
    put(b, 100, 8);      /* global_gain */
    put(b, 0, 1);        /* ics_reserved_bit */
    put(b, 0, 2);        /* ONLY_LONG_SEQUENCE */
    put(b, 0, 1);        /* window_shape */
    put(b, 4, 6);        /* max_sfb */
    put(b, 0, 1);        /* predictor_data_present */
    put(b, 0, 4);        /* sect_cb: ZERO_HCB */
    put(b, 4, 5);        /* sect_len: all four bands */
    put(b, 0, 1);        /* pulse_data_present */
    put(b, 1, 1);        /* tns_data_present */
    put(b, 1, 2);        /* n_filt */
    put(b, 0, 1);        /* coef_res: 3 bits */
    put(b, 4, 6);        /* length */
    put(b, order, 5);    /* order */
    put(b, 0, 1);        /* direction */
    put(b, 0, 1);        /* coef_compress */
    for (unsigned i = 0; i < order; i++) put(b, 1, 3);
    put(b, 0, 1);        /* gain_control_data_present */
    put(b, 7, 3);        /* ID_END */
    return (b->bits + 7) / 8;
}

/* A frame the decoder cannot parse is concealed, not reported as an error. */
static int decodes_cleanly(unsigned order)
{
    static const uint8_t asc[2] = { 0x11, 0x88 };   /* LC, 48 kHz, mono, 1024 */
    faad_config cfg;
    faad_decoder *dec = NULL;
    faad_stream_info si = { .struct_size = sizeof si };
    Bits b;
    uint8_t pcm[16384];
    uint32_t used, wrote;
    faad_config_init(&cfg, sizeof cfg);
    cfg.stream_format = FAAD_STREAM_RAW;
    cfg.output_format = FAAD_OUTPUT_16BIT;
    if (faad_decoder_open(&cfg, asc, sizeof asc, &dec) != FAAD_OK) return -1;
    faad_decoder_get_info(dec, &si);
    unsigned n = frame(&b, order);
    uint32_t flags_out = 0;
    faad_status st = faad_decode_frame(dec, b.buf, n, &used, pcm, sizeof pcm < si.max_output_bytes ? sizeof pcm : si.max_output_bytes, &wrote, &flags_out);
    faad_decoder_close(&dec);
    return st == FAAD_OK && !(flags_out & FAAD_FRAME_CONCEALED);
}

int main(void)
{
    int failed = 0;
    for (unsigned order = 1; order <= 16; order++) {
        int ok = decodes_cleanly(order);
        if (ok != (order <= 12)) {
            fprintf(stderr, "TNS order %u: %s\n", order, ok ? "decoded" : "rejected");
            failed = 1;
        }
    }
    if (!failed) puts("TNS order limit: PASS");
    return failed;
}
