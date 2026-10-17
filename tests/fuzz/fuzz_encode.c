/*
 * libFuzzer harness for the public encoder API.
 *
 * The input is a small header that picks the faac_params (including values the
 * validator must reject), then a stream of PCM chunks whose sizes and contents
 * come from the remaining bytes. Float input carries whatever bit patterns the
 * fuzzer produces, NaN/Inf included.
 *
 * Build and run with tests/fuzz/run.sh.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "faac.h"

#define MAX_FRAME_SAMPLES (2048 * 8)
#define MAX_CALLS 64

static const uint32_t rates[] = {
    8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000, 64000, 88200, 96000
};

typedef struct {
    const uint8_t *p;
    size_t n;
} cursor;

static uint32_t take(cursor *c, unsigned bytes)
{
    uint32_t v = 0;
    while (bytes--) {
        v = (v << 8) | (c->n ? *c->p : 0);
        if (c->n) { c->p++; c->n--; }
    }
    return v;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    cursor c = { data, size };
    faac_params p;
    faac_encoder *enc = NULL;
    faac_encoder_info info;
    static uint8_t out[8192];
    static uint8_t pcm[MAX_FRAME_SAMPLES * sizeof(int32_t)];
    const uint8_t *asc;
    uint32_t asc_len, w, flags, sel, i;
    int32_t map[16];

    if (size < 24)
        return 0;

    faac_params_init(&p, sizeof(p));
    flags = take(&c, 4);
    sel = take(&c, 1);
    p.sample_rate  = (sel & 0x80) ? take(&c, 4) : rates[sel % (sizeof(rates) / sizeof(rates[0]))];
    p.num_channels = take(&c, 1) % 10;
    p.mpeg_version = (enum faac_mpeg_version)(flags & 3);
    p.object_type  = (enum faac_object_type)((flags >> 2 & 3) == 0 ? 0 : (flags >> 2 & 3) == 1 ? 2 :
                                              (flags >> 2 & 3) == 2 ? 5 : 29);
    p.joint_mode   = (enum faac_joint_mode)(flags >> 4 & 7);
    p.use_lfe      = flags >> 7 & 1;
    p.use_tns      = flags >> 8 & 1;
    p.use_pns      = flags >> 9 & 1;
    p.output_format = (enum faac_stream_format)(flags >> 10 & 3);
    p.input_format = (enum faac_input_format)(flags >> 12 & 7);
    p.short_control = (enum faac_shortctl_mode)(flags >> 15 & 3);
    p.rate_control = (enum faac_rate_control)(flags >> 17 & 7);
    p.bit_rate     = (flags >> 20 & 1) ? take(&c, 4) : take(&c, 2) * 4;
    p.bandwidth    = (flags >> 21 & 1) ? take(&c, 4) : take(&c, 2);
    p.quant_quality = (flags >> 22 & 1) ? take(&c, 4) : take(&c, 2) % 6000;
    p.max_bit_rate = (flags >> 23 & 1) ? take(&c, 4) : 0;
    p.reserved[0]  = (flags >> 24 & 0xf) == 15;
    if (flags >> 28 & 1) {
        for (i = 0; i < 16; i++)
            map[i] = (int32_t)(int8_t)take(&c, 1) % 10;
        p.channel_map = map;
        p.channel_map_count = take(&c, 1) % 17;
    }

    if (faac_encoder_open(&p, &enc) != FAAC_OK)
        return 0;

    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    if (faac_encoder_get_info(enc, &info) != FAAC_OK || !info.frame_samples) {
        __builtin_trap();
    }
    (void)faac_encoder_asc(enc, &asc, &asc_len);

    {
        unsigned bps = p.input_format == FAAC_INPUT_16BIT ? 2 : 4;
        uint32_t cap = info.frame_samples * p.num_channels;
        int calls;

        for (calls = 0; calls < MAX_CALLS && c.n; calls++) {
            uint32_t n = take(&c, 2);
            size_t bytes;
            /* mostly full frames, sometimes ragged, sometimes over the limit */
            n = (n & 0xc000) == 0xc000 ? n & 0x3fff : (n % (cap + 1));
            if (n > cap + 8)
                n = cap + 8;
            bytes = (size_t)n * bps;
            if (bytes > sizeof(pcm))
                bytes = sizeof(pcm), n = (uint32_t)(bytes / bps);
            memset(pcm, 0, bytes);
            if (c.n) {
                size_t k = c.n < bytes ? c.n : bytes;
                /* repeat the available seed bytes so short inputs still fill frames */
                for (i = 0; i < bytes; i++)
                    pcm[i] = c.p[i % k];
                c.p += k < c.n ? k : c.n;
                c.n -= k < c.n ? k : c.n;
            }
            (void)faac_encoder_encode(enc, pcm, n, out, sizeof(out), &w);
        }
        for (i = 0; i < 8; i++) {
            if (faac_encoder_encode(enc, NULL, 0, out, sizeof(out), &w) != FAAC_OK || !w)
                break;
        }
    }
    faac_encoder_close(&enc);
    return 0;
}
