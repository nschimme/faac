/* Shared ilst serialization keeps frontend and retrofit tag layouts aligned. */
#include <stdio.h>
#include "libfaam_internal.h"

typedef struct {
    faam_bytes_writer write;
    void *user;
    uint64_t size;
    faam_status error;
} tag_writer;

static void bytes(tag_writer *w, const void *data, size_t length) {
    if (w->error) return;
    if (length > UINT32_MAX || w->size + length > UINT32_MAX) {
        w->error = FAAM_ERR_UNSUPPORTED;
        return;
    }
    w->size += length;
    if (w->write && length) w->error = w->write(w->user, data, (uint32_t)length);
}
static void number(tag_writer *w, uint32_t value) {
    uint8_t data[4]; write_u32_be(data, value); bytes(w, data, sizeof(data));
}
static void header(tag_writer *w, size_t size, const char *type) {
    if (size > UINT32_MAX) { w->error = FAAM_ERR_UNSUPPORTED; return; }
    number(w, (uint32_t)size); bytes(w, type, 4);
}
static void data_tag(tag_writer *w, const char *name, uint32_t type, const void *data, size_t length) {
    if (!data) return;
    if (length > UINT32_MAX - 24) { w->error = FAAM_ERR_UNSUPPORTED; return; }
    header(w, 24 + length, name); header(w, 16 + length, "data");
    number(w, type); number(w, 0); bytes(w, data, length);
}
static void text(tag_writer *w, const char *name, const char *value) {
    if (value) data_tag(w, name, 1, value, strlen(value));
}
static void index_tag(tag_writer *w, const char *name, uint16_t value, uint16_t total) {
    uint8_t data[8] = {0}; write_u16_be(data+2, value); write_u16_be(data+4, total);
    data_tag(w, name, 0, data, sizeof(data));
}
static void custom(tag_writer *w, const char *mean, const char *name, const char *value) {
    if (!name || !value) return;
    size_t m = strlen(mean), n = strlen(name), v = strlen(value);
    if (m > UINT32_MAX - 48 || n > UINT32_MAX - 48 - m || v > UINT32_MAX - 48 - m - n) {
        w->error = FAAM_ERR_UNSUPPORTED; return;
    }
    header(w, 48 + m + n + v, "----");
    header(w, 12 + m, "mean"); number(w, 0); bytes(w, mean, m);
    header(w, 12 + n, "name"); number(w, 0); bytes(w, name, n);
    header(w, 16 + v, "data"); number(w, 1); number(w, 0); bytes(w, value, v);
}

static void emit_ilst(tag_writer *w, uint32_t size, const faam_metadata *m,
                      const faam_gapless_info *gapless, const uint8_t *smpb, uint32_t smpb_bytes) {
    static const faam_metadata empty = {0};
    if (!m) m = &empty;
    header(w, size, "ilst");
    text(w, "\251too", m->encoder);
    text(w, "\251ART", m->artist); text(w, "soar", m->artist_sort);
    text(w, "\251wrt", m->composer); text(w, "soco", m->composer_sort);
    text(w, "\251nam", m->title); text(w, "\251alb", m->album);
    text(w, "aART", m->album_artist); text(w, "soaa", m->album_artist_sort);
    text(w, "soal", m->album_sort); text(w, "\251day", m->year); text(w, "\251cmt", m->comment);
    if (m->genre_code) {
        uint8_t genre[2]; write_u16_be(genre, m->genre_code); data_tag(w, "gnre", 0, genre, 2);
    } else text(w, "\251gen", m->genre_str);
    text(w, "sonm", m->title_sort);
    if (m->compilation) { uint8_t flag = 1; data_tag(w, "cpil", 0x15, &flag, 1); }
    if (m->track_num) index_tag(w, "trkn", m->track_num, m->track_total);
    if (m->disc_num) index_tag(w, "disk", m->disc_num, m->disc_total);
    data_tag(w, "covr", 0x0d, m->cover_art, m->cover_bytes);
    if (gapless) {
        char value[128];
        snprintf(value, sizeof(value),
            " 00000000 %08X %08X %08X%08X 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000",
            gapless->encoder_delay, gapless->end_padding,
            (uint32_t)(gapless->total_samples >> 32), (uint32_t)gapless->total_samples);
        custom(w, "com.apple.iTunes", "iTunSMPB", value);
    } else if (smpb_bytes) bytes(w, smpb, smpb_bytes);
    if (m->num_custom_tags && !m->custom_tags) { w->error = FAAM_ERR_INVALID_ARG; return; }
    for (uint32_t i = 0; i < m->num_custom_tags && !w->error; i++)
        custom(w, "faac", m->custom_tags[i].name, m->custom_tags[i].value);
}

faam_status faam_write_ilst(const faam_metadata *metadata, const faam_gapless_info *gapless,
                            const uint8_t *smpb, uint32_t smpb_bytes,
                            faam_bytes_writer write, void *user, uint32_t *out_size) {
    if (!out_size || (smpb_bytes && !smpb)) return FAAM_ERR_INVALID_ARG;
    tag_writer count = {0};
    emit_ilst(&count, 0, metadata, gapless, smpb, smpb_bytes);
    if (count.error) return count.error;
    *out_size = (uint32_t)count.size;
    if (!write) return FAAM_OK;
    tag_writer output = {0}; output.write = write; output.user = user;
    emit_ilst(&output, *out_size, metadata, gapless, smpb, smpb_bytes);
    return output.error;
}
