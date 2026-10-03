/* Public ABI and packet-boundary regressions. No encoder dependency. */
#include "faad.h"
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
typedef struct {
    uint32_t struct_size, sample_rate, samples_per_ch, channels;
    bool sbr_active, ps_active;
    uint32_t decoder_delay, channel_mask;
    bool concealed, degraded;
    uint8_t reserved[2];
} frame_v3;
#define BASE(T, field) ((uint32_t)(offsetof(T, field) + sizeof(((T *)0)->field)))

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

static void *aligned_storage(uint32_t bytes, void **allocation)
{
    *allocation = malloc((size_t)bytes + FAAD_STATE_ALIGNMENT - 1);
    assert(*allocation);
    uintptr_t ptr = (uintptr_t)*allocation;
    size_t offset = (FAAD_STATE_ALIGNMENT - (ptr & (FAAD_STATE_ALIGNMENT - 1)))
        & (FAAD_STATE_ALIGNMENT - 1);
    return (uint8_t *)*allocation + offset;
}

static const uint8_t mono_adts[] = { 0xff, 0xf1, 0x50, 0x40, 0x01, 0x1f, 0xfc, 0xe0 };
static const uint8_t mono_crc[] = { 0xff, 0xf0, 0x50, 0x40, 0x01, 0x5f, 0xfc, 0, 0, 0xe0 };

static void test_boundaries(void)
{
    faad_config cfg;
    assert(faad_config_init(&cfg, sizeof(cfg)) == FAAD_OK);
    uint32_t bytes;
    assert(faad_get_state_size(&cfg, &bytes) == FAAD_OK);
    void *allocation, *mem = aligned_storage(bytes, &allocation);
    faad_decoder *dec = NULL;
    assert(faad_decoder_init(mem, bytes - 1, &cfg, NULL, 0, &dec) == FAAD_ERR_INSUFFICIENT_MEM);
    assert(!dec);
    assert(faad_decoder_init((uint8_t *)mem + 1, bytes, &cfg, NULL, 0, &dec) == FAAD_ERR_INVALID_ARGUMENT);
    assert(!dec);
    assert(faad_decoder_init(mem, bytes, &cfg, NULL, 0, &dec) == FAAD_OK);
    void *before = malloc(bytes);
    assert(before);
    memcpy(before, mem, bytes);
    faad_stream_info info = { .struct_size = sizeof(info) };
    assert(faad_decoder_get_info(dec, &info) == FAAD_OK);
    uint8_t *pcm = malloc(info.max_output_bytes);
    assert(pcm);
    struct { frame_v3 value; uint8_t tail[16]; } frame;
    memset(&frame, 0xa5, sizeof(frame));
    frame.value.struct_size = sizeof(frame);
    uint32_t used = 99, written = 99;
    assert(faad_decode_frame(dec, mono_adts, sizeof(mono_adts), &used, pcm,
        info.max_output_bytes - 1, &written, (faad_frame_info *)&frame.value) == FAAD_ERR_OUTPUT_TOO_SMALL);
    assert(!used && !written && !frame.value.sample_rate);
    assert_tail(&frame, frame.value.struct_size, sizeof(frame));
    assert(memcmp(before, mem, bytes) == 0);
    for (size_t crc = 0; crc < 2; crc++) {
        const uint8_t *packet = crc ? mono_crc : mono_adts;
        uint32_t len = crc ? sizeof(mono_crc) : sizeof(mono_adts);
        for (uint32_t split = 0; split < len; split++) {
            used = written = 99;
            assert(faad_decode_frame(dec, packet, split, &used, pcm, info.max_output_bytes,
                &written, NULL) == FAAD_ERR_NEED_MORE_DATA);
            assert(!used && !written && memcmp(before, mem, bytes) == 0);
        }
    }
    frame.value.struct_size = BASE(frame_v3, reserved) - 1;
    used = written = 99;
    assert(faad_decode_frame(dec, mono_adts, sizeof(mono_adts), &used, pcm,
        info.max_output_bytes, &written, (faad_frame_info *)&frame.value) == FAAD_ERR_INVALID_ARGUMENT);
    assert(!used && !written && memcmp(before, mem, bytes) == 0);
    const uint8_t lost[] = { 0, 1, 0xff };
    assert(faad_decode_frame(dec, lost, sizeof(lost), &used, pcm, info.max_output_bytes,
        &written, NULL) == FAAD_ERR_SYNC_LOST);
    assert(used == 2 && !written && memcmp(before, mem, bytes) == 0);
    uint8_t unsupported[sizeof(mono_adts)];
    memcpy(unsupported, mono_adts, sizeof(unsupported));
    unsupported[2] &= 0x3f; /* AAC Main */
    assert(faad_decode_frame(dec, unsupported, sizeof(unsupported), &used, pcm,
        info.max_output_bytes, &written, NULL) == FAAD_ERR_UNSUPPORTED);
    assert(used == sizeof(unsupported) && !written && memcmp(before, mem, bytes) == 0);
    memset(&frame, 0xa5, sizeof(frame));
    frame.value.struct_size = BASE(frame_v3, reserved);
    assert(faad_decode_frame(dec, mono_adts, sizeof(mono_adts), &used, pcm,
        info.max_output_bytes, &written, (faad_frame_info *)&frame.value) == FAAD_OK);
    assert(used == sizeof(mono_adts) && written == 2048);
    assert(frame.value.concealed && !frame.value.degraded && frame.value.channel_mask == 4);
    assert_tail(&frame, frame.value.struct_size, sizeof(frame));
    info.struct_size = sizeof(info);
    assert(faad_decoder_get_info(dec, &info) == FAAD_OK && info.format_known && info.channels == 1);
    uint32_t bound = info.max_output_bytes;
    assert(faad_decoder_flush(dec) == FAAD_OK);
    info.struct_size = sizeof(info);
    assert(faad_decoder_get_info(dec, &info) == FAAD_OK && info.max_output_bytes == bound && info.format_known);
    assert(faad_decoder_close(&dec) == FAAD_OK);
    free(pcm); free(before); free(allocation);
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
    faad_frame_info frame = { .struct_size = sizeof(frame) };
    uint32_t used, written;
    assert(faad_decode_frame(dec, packet, sizeof(packet), &used, pcm,
        info.max_output_bytes, &written, &frame) == FAAD_OK);
    assert(used == sizeof(packet) && written > 0 && !frame.concealed);
#ifndef FAAD_DISABLE_SBR
    assert(frame.degraded && frame.sbr_active && frame.decoder_delay == 962);
    info.struct_size = sizeof(info);
    assert(faad_decoder_get_info(dec, &info) == FAAD_OK && info.sample_rate == frame.sample_rate
        && info.decoder_delay == frame.decoder_delay);
    assert(faad_decoder_flush(dec) == FAAD_OK);
    info.struct_size = sizeof(info);
    assert(faad_decoder_get_info(dec, &info) == FAAD_OK && info.decoder_delay == 962);
#else
    assert(!frame.degraded && !frame.sbr_active && frame.decoder_delay == 0);
#endif
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
            faad_frame_info frame = { .struct_size = sizeof(frame) };
            uint32_t used, written;
            const uint8_t *packet = parametric ? end : adts;
            uint32_t len = parametric ? sizeof(end) : sizeof(adts);
            assert(faad_decode_frame(dec, packet, len, &used, pcm, bound, &written, &frame) == FAAD_OK);
            assert(frame.concealed && frame.sbr_active == library.sbr_supported);
            assert(frame.ps_active == (parametric && library.ps_supported));
            assert(frame.channels == (parametric && library.ps_supported ? 2u : 1u));
            assert(frame.sample_rate == (library.sbr_supported ? 32000u : 16000u));
            assert(frame.decoder_delay == (library.sbr_supported ? 962u : 0u));
            info.struct_size = sizeof(info);
            assert(faad_decoder_get_info(dec, &info) == FAAD_OK && info.channels == frame.channels
                && info.sample_rate == frame.sample_rate && info.decoder_delay == frame.decoder_delay);
            assert(faad_decoder_flush(dec) == FAAD_OK);
            info.struct_size = sizeof(info);
            assert(faad_decoder_get_info(dec, &info) == FAAD_OK && info.max_output_bytes == bound
                && info.channels == frame.channels && info.decoder_delay == frame.decoder_delay);
        }
        free(pcm);
        assert(faad_decoder_close(&dec) == FAAD_OK);
    }
}

int main(void)
{
    test_layouts(); test_boundaries(); test_raw_init(); test_endian_helpers(); test_degraded();
    test_known_extensions();
    puts("FAAD ABI, packet boundaries and endian helpers passed");
    return 0;
}
