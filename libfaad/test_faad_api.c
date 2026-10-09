/* Public ABI and packet-boundary regressions. No encoder dependency. */
#include "faad_internal.h"
#include "endian.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Frozen FAAD 3 caller layouts. Do not extend these when the header grows. */
typedef struct {
    uint32_t struct_size, stream_format, output_format, downmix_mode;
} config_v3;
typedef struct {
    uint32_t struct_size;
    const char *version, *copyright;
    uint32_t max_channels;
    bool sbr_supported, ps_supported;
    uint8_t reserved[2];
} library_v3;
typedef struct {
    uint32_t struct_size, sample_rate, channels, object_type, decoder_delay;
    uint32_t frame_samples, max_output_bytes, channel_mask;
    bool format_known;
    uint8_t reserved[3];
} stream_v3;
#define BASE(T, field) ((uint32_t)(offsetof(T, field) + sizeof(((T *)0)->field)))

_Static_assert(sizeof(enum faad_frame_flag) == 4, "frame flag width");
_Static_assert(sizeof(faad_status) == 4, "status width");
_Static_assert(sizeof(enum faad_object_type) == 4, "object width");
_Static_assert(sizeof(enum faad_stream_format) == 4, "stream width");
_Static_assert(sizeof(enum faad_output_format) == 4, "output width");
_Static_assert(sizeof(enum faad_downmix_mode) == 4, "downmix width");
_Static_assert(sizeof(bool) == 1, "public flag width");
_Static_assert(sizeof(float) == 4, "PCM float width");

static void assert_tail(const void *buf, size_t start, size_t end)
{
    const uint8_t *p = buf;
    for (size_t i = start; i < end; i++) assert(p[i] == 0xa5);
}

static void test_layouts(void)
{
    struct { config_v3 value; uint8_t tail[16]; } cfg;
    memset(&cfg, 0xa5, sizeof(cfg));
    assert(faad_config_init((faad_config *)&cfg.value, BASE(config_v3, downmix_mode)) == FAAD_OK);
    assert(cfg.value.struct_size == BASE(config_v3, downmix_mode));
    assert(cfg.value.stream_format == FAAD_STREAM_ADTS && cfg.value.output_format == FAAD_OUTPUT_16BIT);
    assert_tail(&cfg, cfg.value.struct_size, sizeof(cfg));
    faad_decoder *dec;
    assert(faad_decoder_open((faad_config *)&cfg.value, NULL, 0, &dec) == FAAD_OK);
    assert_tail(&cfg, sizeof(config_v3), sizeof(cfg));
    assert(faad_decoder_close(&dec) == FAAD_OK);
    struct { faad_config value; uint8_t tail[16]; } future_cfg;
    memset(&future_cfg, 0xa5, sizeof(future_cfg));
    assert(faad_config_init(&future_cfg.value, sizeof(future_cfg)) == FAAD_OK);
    future_cfg.value.struct_size = sizeof(future_cfg);
    assert(faad_decoder_open(&future_cfg.value, NULL, 0, &dec) == FAAD_OK);
    assert_tail(&future_cfg, sizeof(faad_config), sizeof(future_cfg));
    struct { library_v3 value; uint8_t tail[16]; } lib;
    struct { stream_v3 value; uint8_t tail[16]; } stream;
    for (int future = 0; future < 2; future++) {
        memset(&lib, 0xa5, sizeof(lib));
        lib.value.struct_size = future ? sizeof(lib) : BASE(library_v3, reserved);
        assert(faad_get_library_info((faad_library_info *)&lib.value) == FAAD_OK);
        assert(lib.value.version && lib.value.max_channels > 0);
        assert_tail(&lib, lib.value.struct_size, sizeof(lib));
        memset(&stream, 0xa5, sizeof(stream));
        stream.value.struct_size = future ? sizeof(stream) : BASE(stream_v3, reserved);
        assert(faad_decoder_get_info(dec, (faad_stream_info *)&stream.value) == FAAD_OK);
        assert(!stream.value.format_known && !stream.value.sample_rate && !stream.value.channels);
        assert(!stream.value.object_type && !stream.value.decoder_delay && !stream.value.channel_mask);
        assert(stream.value.max_output_bytes > 0);
        assert_tail(&stream, stream.value.struct_size, sizeof(stream));
    }
    lib.value.struct_size = BASE(library_v3, reserved) - 1;
    assert(faad_get_library_info((faad_library_info *)&lib.value) == FAAD_ERR_INVALID_ARGUMENT);
    assert(faad_decoder_close(&dec) == FAAD_OK);
}

/* ADTS has no optional state before its first accepted packet. */
typedef struct {
    faad_decoder dec;
    float spec[MAX_CHANNELS][FRAME_LEN_LONG];
    float overlap[MAX_CHANNELS][FRAME_LEN_LONG];
    float prev_spec[MAX_CHANNELS][FRAME_LEN_LONG];
    FrameScratch scratch;
    float pcm[MAX_CHANNELS * FRAME_SAMPLES_MAX];
} StateSnapshot;

static void snapshot_state(const faad_decoder *dec, StateSnapshot *snapshot)
{
    memset(snapshot, 0, sizeof(*snapshot));
    memcpy(&snapshot->dec, dec, sizeof(*dec));
    memcpy(snapshot->spec, dec->spec, sizeof(snapshot->spec));
    memcpy(snapshot->overlap, dec->overlap, sizeof(snapshot->overlap));
    memcpy(snapshot->prev_spec, dec->prev_spec, sizeof(snapshot->prev_spec));
    memcpy(&snapshot->scratch, dec->scratch, sizeof(snapshot->scratch));
#ifndef FAAD_DISABLE_SBR
    assert(!dec->sbr && !dec->sbr_el && !dec->sbr_scratch.y);
#endif
#ifndef FAAD_DISABLE_PS
    assert(!dec->ps);
#endif
    memcpy(snapshot->pcm, dec->pcm, sizeof(snapshot->pcm));
}

static bool state_unchanged(const faad_decoder *dec, const StateSnapshot *before,
                            StateSnapshot *after)
{
    snapshot_state(dec, after);
    return memcmp(before, after, sizeof(*before)) == 0;
}

static const uint8_t mono_adts[] = { 0xff, 0xf1, 0x50, 0x40, 0x01, 0x1f, 0xfc, 0xe0 };
static const uint8_t mono_crc[] = { 0xff, 0xf0, 0x50, 0x40, 0x01, 0x5f, 0xfc, 0, 0, 0xe0 };

static void test_boundaries(void)
{
    faad_config cfg;
    assert(faad_config_init(&cfg, sizeof(cfg)) == FAAD_OK);
    faad_decoder *dec = NULL;
    assert(faad_decoder_open(&cfg, NULL, 0, &dec) == FAAD_OK);
    StateSnapshot *before = malloc(sizeof(*before));
    StateSnapshot *after = malloc(sizeof(*after));
    assert(before && after);
    snapshot_state(dec, before);
    faad_stream_info info = { .struct_size = sizeof(info) };
    assert(faad_decoder_get_info(dec, &info) == FAAD_OK);
    uint8_t *pcm = malloc(info.max_output_bytes);
    assert(pcm);
    uint32_t flags = UINT32_MAX;
    uint32_t used = 99, written = 99;
    assert(faad_decode_frame(dec, mono_adts, sizeof(mono_adts), &used, pcm,
        info.max_output_bytes - 1, &written, &flags) == FAAD_ERR_OUTPUT_TOO_SMALL);
    assert(!used && !written && !flags);
    assert(state_unchanged(dec, before, after));
    for (size_t crc = 0; crc < 2; crc++) {
        const uint8_t *packet = crc ? mono_crc : mono_adts;
        uint32_t len = crc ? sizeof(mono_crc) : sizeof(mono_adts);
        for (uint32_t split = 0; split < len; split++) {
            used = written = 99;
            flags = UINT32_MAX;
            assert(faad_decode_frame(dec, packet, split, &used, pcm, info.max_output_bytes,
                &written, &flags) == FAAD_ERR_NEED_MORE_DATA);
            assert(!used && !written && !flags && state_unchanged(dec, before, after));
        }
    }
    flags = UINT32_MAX;
    used = written = 99;
    assert(faad_decode_frame(NULL, mono_adts, sizeof(mono_adts), &used, pcm,
        info.max_output_bytes, &written, &flags) == FAAD_ERR_INVALID_ARGUMENT);
    assert(!used && !written && !flags && state_unchanged(dec, before, after));
    const uint8_t lost[] = { 0, 1, 0xff };
    assert(faad_decode_frame(dec, lost, sizeof(lost), &used, pcm, info.max_output_bytes,
        &written, NULL) == FAAD_ERR_SYNC_LOST);
    assert(used == 2 && !written && state_unchanged(dec, before, after));
    uint8_t unsupported[sizeof(mono_adts)];
    memcpy(unsupported, mono_adts, sizeof(unsupported));
    unsupported[2] &= 0x3f; /* AAC Main */
    assert(faad_decode_frame(dec, unsupported, sizeof(unsupported), &used, pcm,
        info.max_output_bytes, &written, NULL) == FAAD_ERR_UNSUPPORTED);
    assert(used == sizeof(unsupported) && !written && state_unchanged(dec, before, after));
    assert(faad_decode_frame(dec, mono_adts, sizeof(mono_adts), &used, pcm,
        info.max_output_bytes, &written, &flags) == FAAD_OK);
    assert(used == sizeof(mono_adts) && written == 2048);
    assert(flags == (FAAD_FRAME_FORMAT_CHANGED | FAAD_FRAME_CONCEALED));
    info.struct_size = sizeof(info);
    assert(faad_decoder_get_info(dec, &info) == FAAD_OK && info.format_known && info.channels == 1 && info.channel_mask == 4);
    assert(faad_decode_frame(dec, mono_adts, sizeof(mono_adts), &used, pcm,
        info.max_output_bytes, &written, &flags) == FAAD_OK);
    assert(written && flags == FAAD_FRAME_CONCEALED);
    assert(faad_decode_frame(dec, mono_adts, sizeof(mono_adts), &used, pcm,
        info.max_output_bytes, &written, NULL) == FAAD_OK && written);
    flags = UINT32_MAX;
    assert(faad_decode_frame(dec, mono_adts, sizeof(mono_adts) - 1, &used, pcm,
        info.max_output_bytes, &written, &flags) == FAAD_ERR_NEED_MORE_DATA);
    assert(!written && !flags);
    faad_stream_info after_error = { .struct_size = sizeof(after_error) };
    assert(faad_decoder_get_info(dec, &after_error) == FAAD_OK);
    assert(memcmp(&info, &after_error, sizeof(info)) == 0);
    uint32_t bound = info.max_output_bytes;
    assert(faad_decoder_flush(dec) == FAAD_OK);
    info.struct_size = sizeof(info);
    assert(faad_decoder_get_info(dec, &info) == FAAD_OK && info.max_output_bytes == bound && info.format_known);
    assert(faad_decode_frame(dec, mono_adts, sizeof(mono_adts), &used, pcm,
        info.max_output_bytes, &written, &flags) == FAAD_OK);
    assert(written && !(flags & FAAD_FRAME_FORMAT_CHANGED));
    assert(faad_decoder_close(&dec) == FAAD_OK);
    free(pcm); free(before); free(after);
}

static void test_raw_init(void)
{
    faad_config cfg;
    assert(faad_config_init(&cfg, sizeof(cfg)) == FAAD_OK);
    cfg.stream_format = FAAD_STREAM_RAW;
    faad_decoder *dec;
    const uint8_t asc[] = { 0x12, 0x08 };
    assert(faad_decoder_open(&cfg, NULL, 0, &dec) == FAAD_ERR_INVALID_ARGUMENT && !dec);
    assert(faad_decoder_open(&cfg, asc, 0, &dec) == FAAD_ERR_INVALID_ARGUMENT && !dec);
    assert(faad_decoder_open(&cfg, NULL, 2, &dec) == FAAD_ERR_INVALID_ARGUMENT && !dec);
    assert(faad_decoder_open(&cfg, asc, 1, &dec) == FAAD_ERR_INVALID_ARGUMENT && !dec);
    const uint8_t zero[] = { 0, 0 };
    assert(faad_decoder_open(&cfg, zero, sizeof(zero), &dec) != FAAD_OK && !dec);
    const uint8_t truncated[] = { 0x17, 0x80 }; /* explicit rate missing */
    assert(faad_decoder_open(&cfg, truncated, sizeof(truncated), &dec) == FAAD_ERR_INVALID_ARGUMENT && !dec);
    assert(faad_decoder_open(&cfg, asc, sizeof(asc), &dec) == FAAD_OK);
    faad_stream_info info = { .struct_size = sizeof(info) };
    assert(faad_decoder_get_info(dec, &info) == FAAD_OK && info.format_known && info.decoder_delay == 0);
    assert(faad_decoder_close(&dec) == FAAD_OK);
}

static void test_endian_helpers(void)
{
    const int32_t values[] = { -8388608, -123456, -1, 0, 1, 123456, 8388607 };
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        uint8_t le[3], be[3];
        write_pcm24(le, values[i], false);
        write_pcm24(be, values[i], true);
        assert(read_pcm24(le, false) == values[i] && read_pcm24(be, true) == values[i]);
        assert(le[0] == be[2] && le[1] == be[1] && le[2] == be[0]);
    }
}

static void put_bits(uint8_t *buf, uint32_t *pos, uint32_t value, uint32_t count)
{
    for (uint32_t i = count; i > 0; i--, (*pos)++)
        buf[*pos / 8] |= (uint8_t)(((value >> (i - 1)) & 1) << (7 - *pos % 8));
}

static uint32_t make_pns_core(uint8_t *packet)
{
    uint32_t pos = 0;
    put_bits(packet, &pos, 0, 3);   /* SCE */
    put_bits(packet, &pos, 0, 4);   /* tag */
    put_bits(packet, &pos, 100, 8); /* global_gain */
    put_bits(packet, &pos, 0, 4);   /* long ICS header before max_sfb */
    put_bits(packet, &pos, 1, 6);   /* one scalefactor band */
    put_bits(packet, &pos, 0, 1);   /* no prediction */
    put_bits(packet, &pos, 13, 4);  /* PNS codebook */
    put_bits(packet, &pos, 1, 5);   /* one-band section */
    put_bits(packet, &pos, 300, 9); /* first PNS energy */
    put_bits(packet, &pos, 0, 3);   /* pulse/TNS/gain flags */
    put_bits(packet, &pos, 7, 3);   /* END */
    return (pos + 7) / 8;
}

static void test_nonzero_conceal_history(void)
{
    faad_config cfg;
    faad_config_init(&cfg, sizeof(cfg));
    cfg.stream_format = FAAD_STREAM_RAW;
    const uint8_t asc[] = { 0x12, 0x08 };
    faad_decoder *dec;
    assert(faad_decoder_open(&cfg, asc, sizeof(asc), &dec) == FAAD_OK);
    faad_stream_info info = { .struct_size = sizeof(info) };
    assert(faad_decoder_get_info(dec, &info) == FAAD_OK);
    void *pcm = malloc(info.max_output_bytes);
    assert(pcm);
    uint8_t pns[8] = { 0 };
    uint32_t pns_len = make_pns_core(pns), used, written, flags;
    const uint8_t end[] = { 0xe0 };
    assert(faad_decode_frame(dec, pns, pns_len, &used, pcm, info.max_output_bytes,
        &written, &flags) == FAAD_OK);
    assert(!(flags & FAAD_FRAME_CONCEALED) && written == 2048);
    bool nonzero = false;
    for (uint32_t i = 0; i < written; i++) nonzero |= ((uint8_t *)pcm)[i] != 0;
    assert(nonzero);

    assert(faad_decode_frame(dec, end, sizeof(end), &used, pcm, info.max_output_bytes,
        &written, &flags) == FAAD_OK);
    assert((flags & FAAD_FRAME_CONCEALED) && written == 2048);
    nonzero = false;
    for (uint32_t i = 0; i < written; i++) nonzero |= ((uint8_t *)pcm)[i] != 0;
    assert(nonzero);
    assert(faad_decode_frame(dec, pns, pns_len, &used, pcm, info.max_output_bytes,
        &written, &flags) == FAAD_OK);
    assert(!(flags & FAAD_FRAME_CONCEALED) && written == 2048);

    assert(faad_decoder_flush(dec) == FAAD_OK);
    assert(faad_decode_frame(dec, end, sizeof(end), &used, pcm, info.max_output_bytes,
        &written, &flags) == FAAD_OK);
    assert((flags & FAAD_FRAME_CONCEALED) && written == 2048);
    for (uint32_t i = 0; i < written; i++) assert(((uint8_t *)pcm)[i] == 0);
    free(pcm);
    assert(faad_decoder_close(&dec) == FAAD_OK);
}

#if MAX_CHANNELS >= 2
static uint32_t make_silent_adts(uint8_t *packet, uint32_t channel_config)
{
    uint8_t payload[8] = { 0 };
    uint32_t pos = 0;
    if (channel_config == 1) {
        put_bits(payload, &pos, 0, 3);   /* SCE */
        put_bits(payload, &pos, 0, 4);   /* tag */
        put_bits(payload, &pos, 100, 8); /* global_gain */
        put_bits(payload, &pos, 0, 11);  /* long ICS, max_sfb=0, no prediction */
        put_bits(payload, &pos, 0, 3);   /* pulse/TNS/gain flags */
    } else {
        put_bits(payload, &pos, 1, 3);   /* CPE */
        put_bits(payload, &pos, 0, 4);   /* tag */
        put_bits(payload, &pos, 0, 1);   /* no common window */
        for (int ch = 0; ch < 2; ch++) {
            put_bits(payload, &pos, 100, 8); /* global_gain */
            put_bits(payload, &pos, 0, 11);  /* long ICS, max_sfb=0, no prediction */
            put_bits(payload, &pos, 0, 3);   /* pulse/TNS/gain flags */
        }
    }
    put_bits(payload, &pos, 7, 3); /* END */
    uint32_t payload_bytes = (pos + 7) / 8;
    uint32_t frame_len = 7 + payload_bytes;
    packet[0] = 0xff;
    packet[1] = 0xf1;
    packet[2] = 0x50; /* AAC-LC, 44.1 kHz */
    packet[3] = (uint8_t)(channel_config << 6);
    packet[4] = (uint8_t)(frame_len >> 3);
    packet[5] = (uint8_t)(((frame_len & 7) << 5) | 0x1f);
    packet[6] = 0xfc;
    memcpy(packet + 7, payload, payload_bytes);
    return frame_len;
}

static void test_channel_history_rotation(void)
{
    faad_config cfg;
    faad_config_init(&cfg, sizeof(cfg));
    faad_decoder *dec;
    assert(faad_decoder_open(&cfg, NULL, 0, &dec) == FAAD_OK);
    faad_stream_info info = { .struct_size = sizeof(info) };
    assert(faad_decoder_get_info(dec, &info) == FAAD_OK);
    void *pcm = malloc(info.max_output_bytes);
    assert(pcm);

    uint8_t packet[15];
    uint32_t len = make_silent_adts(packet, 1), used, written, flags;
    assert(faad_decode_frame(dec, packet, len, &used, pcm, info.max_output_bytes,
        &written, &flags) == FAAD_OK);
    assert(written == 2048 && flags == FAAD_FRAME_FORMAT_CHANGED);

    /* A stale right-channel history must not enter the next decoded CPE. */
    dec->prev_spec[1][0] = 16384.0f;
    len = make_silent_adts(packet, 2);
    assert(faad_decode_frame(dec, packet, len, &used, pcm, info.max_output_bytes,
        &written, &flags) == FAAD_OK);
    assert(written == 4096 && flags == FAAD_FRAME_FORMAT_CHANGED);
    for (uint32_t i = 0; i < written; i++) assert(((uint8_t *)pcm)[i] == 0);
    info.struct_size = sizeof(info);
    assert(faad_decoder_get_info(dec, &info) == FAAD_OK && info.channels == 2);

    /* A mono success must leave the inactive right-channel concealment history intact. */
    dec->prev_spec[1][0] = 16384.0f;
    len = make_silent_adts(packet, 1);
    assert(faad_decode_frame(dec, packet, len, &used, pcm, info.max_output_bytes,
        &written, &flags) == FAAD_OK);
    assert(written == 2048 && flags == FAAD_FRAME_FORMAT_CHANGED);
    assert(dec->prev_spec[1][0] == 16384.0f);
    const uint8_t end[] = { 0xe0 };
    packet[0] = 0xff;
    packet[1] = 0xf1;
    packet[2] = 0x50;
    packet[3] = 0x80;
    packet[4] = 1;
    packet[5] = 0x1f;
    packet[6] = 0xfc;
    memcpy(packet + 7, end, sizeof(end));
    len = 8;
    assert(faad_decode_frame(dec, packet, len, &used, pcm, info.max_output_bytes,
        &written, &flags) == FAAD_OK);
    assert(written == 4096 && (flags & (FAAD_FRAME_CONCEALED | FAAD_FRAME_FORMAT_CHANGED))
        == (FAAD_FRAME_CONCEALED | FAAD_FRAME_FORMAT_CHANGED));
    bool right_nonzero = false;
    const int16_t *samples = (const int16_t *)pcm;
    for (uint32_t i = 0; i < 1024; i++) right_nonzero |= samples[2 * i + 1] != 0;
    assert(right_nonzero);

    len = make_silent_adts(packet, 1);
    assert(faad_decode_frame(dec, packet, len, &used, pcm, info.max_output_bytes,
        &written, &flags) == FAAD_OK);
    assert(written == 2048 && !(flags & FAAD_FRAME_CONCEALED));
    len = make_silent_adts(packet, 2);
    assert(faad_decode_frame(dec, packet, len, &used, pcm, info.max_output_bytes,
        &written, &flags) == FAAD_OK);
    assert(written == 4096 && (flags & FAAD_FRAME_FORMAT_CHANGED)
        && !(flags & FAAD_FRAME_CONCEALED));

    len = make_silent_adts(packet, 1);
    assert(faad_decode_frame(dec, packet, len, &used, pcm, info.max_output_bytes,
        &written, &flags) == FAAD_OK);
    assert(written == 2048 && flags == FAAD_FRAME_FORMAT_CHANGED);
    len = make_silent_adts(packet, 2);

    assert(faad_decoder_flush(dec) == FAAD_OK);
    assert(faad_decode_frame(dec, packet, len, &used, pcm, info.max_output_bytes,
        &written, &flags) == FAAD_OK);
    assert(written == 4096 && flags == FAAD_FRAME_FORMAT_CHANGED);
    for (uint32_t i = 0; i < written; i++) assert(((uint8_t *)pcm)[i] == 0);
    free(pcm);
    assert(faad_decoder_close(&dec) == FAAD_OK);
}
#endif

static void test_degraded(void)
{
    /* Valid silent SCE, followed by an SBR header whose stop band is below
     * its start band. Core audio remains usable; SBR must report recovery. */
    uint8_t packet[8] = { 0 };
    uint32_t pos = 0;
    put_bits(packet, &pos, 0, 3); /* SCE */
    put_bits(packet, &pos, 0, 4); /* tag */
    put_bits(packet, &pos, 100, 8); /* global_gain */
    put_bits(packet, &pos, 0, 11); /* long ICS, max_sfb=0, no prediction */
    put_bits(packet, &pos, 0, 3); /* pulse/TNS/gain flags */
    put_bits(packet, &pos, 6, 3); /* FIL */
    put_bits(packet, &pos, 3, 4); /* three bytes */
    put_bits(packet, &pos, 13, 4); /* SBR extension */
    put_bits(packet, &pos, 1, 1); /* header_present */
    put_bits(packet, &pos, 1, 1); /* amp_res */
    put_bits(packet, &pos, 15, 4); /* start_freq */
    put_bits(packet, &pos, 0, 4); /* stop_freq */
    put_bits(packet, &pos, 0, 10); /* remaining header and padding */
    put_bits(packet, &pos, 7, 3); /* END */
    assert(pos == 63);
    faad_config cfg;
    faad_config_init(&cfg, sizeof(cfg));
    cfg.stream_format = FAAD_STREAM_RAW;
    const uint8_t asc[] = { 0x12, 0x08 };
    faad_decoder *dec;
    assert(faad_decoder_open(&cfg, asc, sizeof(asc), &dec) == FAAD_OK);
    faad_stream_info info = { .struct_size = sizeof(info) };
    assert(faad_decoder_get_info(dec, &info) == FAAD_OK);
    void *pcm = malloc(info.max_output_bytes);
    assert(pcm);
    uint32_t flags;
    uint32_t used, written;
    const uint8_t core[] = { 0, 0xc8, 0, 7 }; /* Silent SCE and END */
    assert(faad_decode_frame(dec, core, sizeof(core), &used, pcm,
        info.max_output_bytes, &written, &flags) == FAAD_OK);
    assert(written == 2048 && flags == FAAD_FRAME_FORMAT_CHANGED);
    assert(faad_decode_frame(dec, core, sizeof(core), &used, pcm,
        info.max_output_bytes, &written, &flags) == FAAD_OK);
    assert(written == 2048 && !flags);

    assert(faad_decode_frame(dec, packet, sizeof(packet), &used, pcm,
        info.max_output_bytes, &written, &flags) == FAAD_OK);
    assert(used == sizeof(packet) && written > 0 && !(flags & FAAD_FRAME_CONCEALED));
#ifndef FAAD_DISABLE_SBR
    assert(flags & FAAD_FRAME_FORMAT_CHANGED);
#else
    assert(!(flags & FAAD_FRAME_FORMAT_CHANGED));
#endif
    info.struct_size = sizeof(info);
    assert(faad_decoder_get_info(dec, &info) == FAAD_OK);
#ifndef FAAD_DISABLE_SBR
    assert((flags & (FAAD_FRAME_DEGRADED | FAAD_FRAME_SBR))
        == (FAAD_FRAME_DEGRADED | FAAD_FRAME_SBR));
    assert(info.sample_rate == 88200 && info.frame_samples == 2048 && info.decoder_delay == 962);
#else
    assert(!(flags & (FAAD_FRAME_DEGRADED | FAAD_FRAME_SBR)) && info.decoder_delay == 0);
#endif
    assert(faad_decoder_flush(dec) == FAAD_OK);
    faad_stream_info after = { .struct_size = sizeof(after) };
    assert(faad_decoder_get_info(dec, &after) == FAAD_OK);
    assert(memcmp(&info, &after, sizeof(info)) == 0);
    free(pcm);
    assert(faad_decoder_close(&dec) == FAAD_OK);
}

static void test_known_extensions(void)
{
    const uint8_t he[] = { 0x2c, 0x0a, 0x88, 0x00 };
    const uint8_t ps[] = { 0xec, 0x0a, 0x88, 0x00 };
    const uint8_t end[] = { 0xe0 };
    uint8_t adts[sizeof(mono_adts)];
    memcpy(adts, mono_adts, sizeof(adts));
    adts[2] = 0x60; /* 16 kHz core, matching ASC */
    faad_library_info library = { .struct_size = sizeof(library) };
    assert(faad_get_library_info(&library) == FAAD_OK);
    for (int parametric = 0; parametric < 2; parametric++) {
        faad_config cfg;
        faad_config_init(&cfg, sizeof(cfg));
        cfg.stream_format = parametric ? FAAD_STREAM_RAW : FAAD_STREAM_ADTS;
        faad_decoder *dec;
        assert(faad_decoder_open(&cfg, parametric ? ps : he, sizeof(he), &dec) == FAAD_OK);
        faad_stream_info info = { .struct_size = sizeof(info) };
        assert(faad_decoder_get_info(dec, &info) == FAAD_OK && info.format_known);
        uint32_t bound = info.max_output_bytes;
        void *pcm = malloc(bound);
        assert(pcm);
        for (int seek = 0; seek < 2; seek++) {
            uint32_t flags;
            uint32_t used, written;
            const uint8_t *packet = parametric ? end : adts;
            uint32_t len = parametric ? sizeof(end) : sizeof(adts);
            assert(faad_decode_frame(dec, packet, len, &used, pcm, bound, &written, &flags) == FAAD_OK);
            assert(flags & FAAD_FRAME_CONCEALED);
            assert(!!(flags & FAAD_FRAME_SBR) == library.sbr_supported);
            assert(!!(flags & FAAD_FRAME_PS) == (parametric && library.ps_supported));
            assert(!!(flags & FAAD_FRAME_FORMAT_CHANGED) == (seek == 0));
            info.struct_size = sizeof(info);
            assert(faad_decoder_get_info(dec, &info) == FAAD_OK);
            assert(info.channels == (parametric && library.ps_supported ? 2u : 1u));
            assert(info.sample_rate == (library.sbr_supported ? 32000u : 16000u));
            assert(info.decoder_delay == (library.sbr_supported ? 962u : 0u));
            assert(faad_decoder_flush(dec) == FAAD_OK);
            faad_stream_info after = { .struct_size = sizeof(after) };
            assert(faad_decoder_get_info(dec, &after) == FAAD_OK);
            assert(after.max_output_bytes == bound && memcmp(&info, &after, sizeof(info)) == 0);
        }
        free(pcm);
        assert(faad_decoder_close(&dec) == FAAD_OK);
    }
}

static void test_ps_mono_downmix(void)
{
    const uint8_t ps_asc[] = { 0xec, 0x0a, 0x88, 0x00 };
    const uint8_t end[] = { 0xe0 };
    faad_library_info library = { .struct_size = sizeof(library) };
    assert(faad_get_library_info(&library) == FAAD_OK);

    const enum faad_downmix_mode modes[] = {
        FAAD_DOWNMIX_MONO,
        FAAD_DOWNMIX_STEREO,
        FAAD_DOWNMIX_NONE
    };

    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
        enum faad_downmix_mode mode = modes[i];
        faad_config cfg;
        faad_config_init(&cfg, sizeof(cfg));
        cfg.stream_format = FAAD_STREAM_RAW;
        cfg.downmix_mode = mode;

        faad_decoder *dec;
        assert(faad_decoder_open(&cfg, ps_asc, sizeof(ps_asc), &dec) == FAAD_OK);
        faad_stream_info info = { .struct_size = sizeof(info) };
        assert(faad_decoder_get_info(dec, &info) == FAAD_OK && info.format_known);
        uint32_t expected_channels = (mode == FAAD_DOWNMIX_MONO) ? 1 : (library.ps_supported ? 2 : 1);
        assert(info.channels == expected_channels);
        assert(info.object_type == FAAD_OBJ_HE_AAC_V2);

        uint32_t bound = info.max_output_bytes;
        void *pcm = malloc(bound);
        assert(pcm);

        uint32_t flags = 0, used = 0, written = 0;
        assert(faad_decode_frame(dec, end, sizeof(end), &used, pcm, bound, &written, &flags) == FAAD_OK);
        assert(flags & FAAD_FRAME_CONCEALED);
        assert(!!(flags & FAAD_FRAME_SBR) == library.sbr_supported);
        assert(!!(flags & FAAD_FRAME_PS) == library.ps_supported);
        assert(written == 2048 * expected_channels * sizeof(int16_t));

        info.struct_size = sizeof(info);
        assert(faad_decoder_get_info(dec, &info) == FAAD_OK);
        assert(info.channels == expected_channels);

        free(pcm);
        assert(faad_decoder_close(&dec) == FAAD_OK);
    }
}

int main(void)
{
    test_layouts(); test_boundaries(); test_raw_init(); test_endian_helpers(); test_degraded();
#if MAX_CHANNELS >= 2
    test_channel_history_rotation();
#endif
    test_nonzero_conceal_history();
    test_known_extensions();
    test_ps_mono_downmix();
    puts("FAAD ABI, packet boundaries and endian helpers passed");
    return 0;
}
