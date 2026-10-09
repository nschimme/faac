/*
 * FAAM - Freeware Advanced Audio Muxer
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

/*
 * Stream-based Demuxer Engine for libfaam (Recursive ISO MP4 Box Walker)
 * Supports audio and video tracks (H.264, H.265, AAC, PCM) and chapters.
 */

#include <stdio.h>
#include <inttypes.h>
#include "endian.h"
#include "libfaam_internal.h"

/* Borrowed views into moov, valid only while constructing the sample index. */
typedef struct {
    const uint8_t *stsz[FAAM_MAX_TRACKS];
    uint32_t  num_stsz[FAAM_MAX_TRACKS];
    uint32_t  fixed_size[FAAM_MAX_TRACKS];
    const uint8_t *stsc[FAAM_MAX_TRACKS];
    uint32_t  num_stsc[FAAM_MAX_TRACKS];
    const uint8_t *stco[FAAM_MAX_TRACKS];
    uint32_t  num_stco[FAAM_MAX_TRACKS];
    const uint8_t *stts[FAAM_MAX_TRACKS];
    uint32_t  num_stts[FAAM_MAX_TRACKS];
    const uint8_t *stss[FAAM_MAX_TRACKS];
    uint32_t  num_stss[FAAM_MAX_TRACKS];
    const uint8_t *ctts[FAAM_MAX_TRACKS];
    uint32_t  num_ctts[FAAM_MAX_TRACKS];
    bool co64[FAAM_MAX_TRACKS];
} faam_trak_tables;

/* A moov this large cannot be a camera or audiobook file; refuse it before allocating. */
#define MOOV_MAX_BYTES (256U << 20)

/* Reads n bytes at pos, retrying short reads. Fewer than n means EOF or a failed seek
 * (a truncated box can point past the end); -1 means the read callback failed. */
static int32_t read_at(const faam_io *io, uint64_t pos, void *out, uint32_t n)
{
    if (!io->seek(io->user_data, pos)) return 0;
    return faam_io_read_full(io, out, n);
}

/* MPEG-4 descriptor header: tag, then a length of up to four 7-bit groups. */
static bool descriptor_head(const uint8_t **p, uint32_t *n, uint8_t *tag, uint32_t *len)
{
    if (*n < 2) return false;
    *tag = (*p)[0];
    (*p)++; (*n)--;
    uint32_t v = 0;
    for (int i = 0; i < 4 && *n; i++) {
        uint8_t b = *(*p)++;
        (*n)--;
        v = (v << 7) | (b & 0x7f);
        if (!(b & 0x80)) { *len = v; return v <= *n; }
    }
    return false;
}

/* MPEG-4 audio, or the MPEG-2 AAC types (Main, LC, SSR) older encoders write; the
 * profile is left to the decoder, which reads it from the AudioSpecificConfig. */
static bool is_aac_object_type(uint8_t oti)
{
    return oti == 0x40 || (oti >= 0x66 && oti <= 0x68);
}

/* Finds the AudioSpecificConfig inside an esds payload: ES_Descriptor, then
 * DecoderConfigDescriptor (MPEG-4 audio only), then DecoderSpecificInfo. */
static bool esds_decoder_config(const uint8_t *p, uint32_t n, const uint8_t **asc, uint32_t *asc_len)
{
    uint8_t tag;
    uint32_t len;
    if (n < 4) return false;
    p += 4; n -= 4;
    if (!descriptor_head(&p, &n, &tag, &len) || tag != 0x03 || len < 3) return false;
    uint8_t flags = p[2];
    uint32_t skip = 3;
    if (flags & 0x80) skip += 2;
    if (flags & 0x40) skip += 1 + (skip + 1 <= len ? p[skip] : 0);
    if (flags & 0x20) skip += 2;
    if (skip > len) return false;
    p += skip; n = len - skip;
    if (!descriptor_head(&p, &n, &tag, &len) || tag != 0x04 || len < 13 || !is_aac_object_type(p[0])) return false;
    p += 13; n = len - 13;
    if (!descriptor_head(&p, &n, &tag, &len) || tag != 0x05 || !len) return false;
    *asc = p;
    *asc_len = len;
    return true;
}

static const char *own_string(faam_demuxer *d, const uint8_t *text, uint32_t len) {
    if (!text) return NULL;
    if ((size_t)len > SIZE_MAX - sizeof(faam_owned_string) - 1) {
        d->error = FAAM_ERR_INSUFFICIENT_MEM;
        return NULL;
    }
    faam_owned_string *s = (faam_owned_string *)AllocMemory(sizeof(*s) + (size_t)len + 1);
    if (!s) { d->error = FAAM_ERR_INSUFFICIENT_MEM; return NULL; }
    s->next = d->strings;
    if (len) memcpy(s->text, text, len);
    s->text[len] = '\0';
    d->strings = s;
    return s->text;
}

#define FAAM_ILST_SET_STR(field) meta->field = own_string(d, val, val_len)

/* Strings returned to callers outlive the temporary container parse buffer. */
static void parse_ilst_children(const uint8_t *buf, long offset, long end, struct faam_demuxer *d)
{
    faam_metadata *meta = &d->metadata;
    long cur = offset;
    while (cur + 8 <= end) {
        uint32_t size = read_u32_be(buf + cur);
        char name[4];
        memcpy(name, buf + cur + 4, 4);
        if (size < 8 || cur + (long)size > end) break;
        long content_end = cur + (long)size;
        long data_off = cur + 8;

        if (memcmp(name, "----", 4) == 0) {
            const uint8_t *tagmean = NULL;
            uint32_t tagmean_len = 0;
            const uint8_t *tagname = NULL;
            uint32_t tagname_len = 0;
            const uint8_t *tval = NULL;
            uint32_t tval_len = 0;
            long p = data_off;
            while (p + 8 <= content_end) {
                uint32_t sub_size = read_u32_be(buf + p);
                char sub_type[4];
                memcpy(sub_type, buf + p + 4, 4);
                if (sub_size < 8 || p + (long)sub_size > content_end) break;
                if (memcmp(sub_type, "mean", 4) == 0 && sub_size > 12) {
                    tagmean = buf + p + 12;
                    tagmean_len = sub_size - 12;
                } else if (memcmp(sub_type, "name", 4) == 0 && sub_size > 12) {
                    tagname = buf + p + 12;
                    tagname_len = sub_size - 12;
                } else if (memcmp(sub_type, "data", 4) == 0 && sub_size >= 16) {
                    tval = buf + p + 16;
                    tval_len = sub_size - 16;
                }
                p += sub_size;
            }
            if (tval && tagname_len == 8 && !memcmp(tagname, "iTunSMPB", 8)) {
                /* Gapless info, not a user tag: " 00000000 <priming> <padding> <length> ..." */
                char str_buf[128] = { 0 };
                uint32_t slen = tval_len < sizeof(str_buf) - 1 ? tval_len : (uint32_t)sizeof(str_buf) - 1;
                memcpy(str_buf, tval, slen);
                if (sscanf(str_buf, " %*x %x %x %" SCNx64, &d->gapless.encoder_delay, &d->gapless.end_padding, &d->gapless.total_samples) >= 2)
                    d->has_gapless = true;
            } else if (tagname_len && tval) {
                uint32_t count = meta->num_custom_tags;
                if ((size_t)count >= SIZE_MAX / sizeof(faam_custom_tag) - 1) {
                    d->error = FAAM_ERR_INSUFFICIENT_MEM;
                } else {
                    faam_custom_tag *tags = d->custom_tags;
                    if (count == d->custom_tags_cap) {
                        size_t cap = d->custom_tags_cap ? (size_t)d->custom_tags_cap * 2 : 8;
                        if (cap > UINT32_MAX || cap > SIZE_MAX / sizeof(*tags)) {
                            d->error = FAAM_ERR_INSUFFICIENT_MEM;
                        } else {
                            tags = (faam_custom_tag *)faam_grow_memory(tags,
                                (size_t)count * sizeof(*tags), cap * sizeof(*tags));
                            if (!tags) d->error = FAAM_ERR_INSUFFICIENT_MEM;
                            else {
                                d->custom_tags = tags;
                                d->custom_tags_cap = (uint32_t)cap;
                            }
                        }
                    }
                    if (!d->error) {
                        meta->custom_tags = tags;
                        tags[count].struct_size = sizeof(faam_custom_tag);
                        tags[count].mean = tagmean_len ? own_string(d, tagmean, tagmean_len) : NULL;
                        tags[count].name = own_string(d, tagname, tagname_len);
                        tags[count].value = own_string(d, tval, tval_len);
                        meta->num_custom_tags++;
                    }
                }
            }

            cur += size;
            continue;
        }

        if (data_off + 16 > content_end) { cur += size; continue; }
        uint32_t data_box_size = read_u32_be(buf + data_off);
        if (memcmp(buf + data_off + 4, "data", 4) != 0 || data_off + (long)data_box_size > content_end || data_box_size < 16) {
            cur += size;
            continue;
        }
        const uint8_t *val = buf + data_off + 16;
        uint32_t val_len = data_box_size - 16;

        if (!memcmp(name, "\251nam", 4)) FAAM_ILST_SET_STR(title);
        else if (!memcmp(name, "sonm", 4)) FAAM_ILST_SET_STR(title_sort);
        else if (!memcmp(name, "\251ART", 4)) FAAM_ILST_SET_STR(artist);
        else if (!memcmp(name, "soar", 4)) FAAM_ILST_SET_STR(artist_sort);
        else if (!memcmp(name, "\251alb", 4)) FAAM_ILST_SET_STR(album);
        else if (!memcmp(name, "soal", 4)) FAAM_ILST_SET_STR(album_sort);
        else if (!memcmp(name, "aART", 4)) FAAM_ILST_SET_STR(album_artist);
        else if (!memcmp(name, "soaa", 4)) FAAM_ILST_SET_STR(album_artist_sort);
        else if (!memcmp(name, "\251wrt", 4)) FAAM_ILST_SET_STR(composer);
        else if (!memcmp(name, "soco", 4)) FAAM_ILST_SET_STR(composer_sort);
        else if (!memcmp(name, "\251day", 4)) FAAM_ILST_SET_STR(year);
        else if (!memcmp(name, "\251cmt", 4)) FAAM_ILST_SET_STR(comment);
        else if (!memcmp(name, "\251too", 4)) FAAM_ILST_SET_STR(encoder);
        else if (!memcmp(name, "\251gen", 4)) FAAM_ILST_SET_STR(genre_str);
        else if (!memcmp(name, "gnre", 4) && val_len >= 2) meta->genre_code = read_u16_be(val);
        else if (!memcmp(name, "cpil", 4) && val_len >= 1) meta->compilation = val[0] != 0;
        else if (!memcmp(name, "trkn", 4) && val_len >= 6) {
            meta->track_num = read_u16_be(val + 2);
            meta->track_total = read_u16_be(val + 4);
        } else if (!memcmp(name, "disk", 4) && val_len >= 6) {
            meta->disc_num = read_u16_be(val + 2);
            meta->disc_total = read_u16_be(val + 4);
        } else if (!memcmp(name, "covr", 4) && val_len > 0) {
            uint8_t *owned = (uint8_t *)AllocMemory(val_len);
            if (!owned) d->error = FAAM_ERR_INSUFFICIENT_MEM;
            if (owned) {
                memcpy(owned, val, val_len);
                if (d->cover_art_owned) FreeMemory(d->cover_art_owned);
                d->cover_art_owned = owned;
                meta->cover_art = owned;
                meta->cover_bytes = val_len;
                /* Honor declared types; sniff only unrecognized declarations. */
                uint32_t declared = read_u32_be(buf + data_off + 8) & 0x00ffffff;
                switch (declared) {
                case 12: meta->cover_type = FAAM_COVER_GIF; break;
                case 13: meta->cover_type = FAAM_COVER_JPEG; break;
                case 14: meta->cover_type = FAAM_COVER_PNG; break;
                case 27: meta->cover_type = FAAM_COVER_BMP; break;
                default:
                    if (val_len >= 8 && !memcmp(val, "\x89PNG\r\n\x1a\n", 8))
                        meta->cover_type = FAAM_COVER_PNG;
                    else if (val_len >= 6 &&
                             (!memcmp(val, "GIF87a", 6) || !memcmp(val, "GIF89a", 6)))
                        meta->cover_type = FAAM_COVER_GIF;
                    else
                        meta->cover_type = FAAM_COVER_JPEG; /* JPEG signature or fallback. */
                    break;
                }
            }
        }

        cur += size;
    }
}
#undef FAAM_ILST_SET_STR

/* Real files nest moov > trak > mdia > minf > stbl > stsd > mp4a and
 * udta > meta; a crafted file must not be able to exhaust the stack. */
#define MAX_BOX_DEPTH 12

static void parse_boxes_recursive(const uint8_t *buf, long offset, long end, struct faam_demuxer *d,
                                  int current_trak_idx, faam_trak_tables *tb, int depth)
{
    if (depth > MAX_BOX_DEPTH) return;
    long cur = offset;
    while (cur + 8 <= end) {
        uint64_t box_size = read_u32_be(buf + cur);
        char type[5] = {0};
        memcpy(type, buf + cur + 4, 4);

        long header_size = 8;
        if (box_size == 1) {
            if (cur + 16 > end) break;
            box_size = read_u64_be(buf + cur + 8);
            header_size = 16;
        } else if (box_size == 0) {
            box_size = (uint64_t)(end - cur);
        }

        /* Compared unsigned: a size above LONG_MAX (64-bit sizes, or any size where long
         * is 32 bits) would turn negative as a long and pass the check. */
        if (box_size < (uint64_t)header_size || box_size > (uint64_t)(end - cur)) break;

        long payload_offset = cur + header_size;
        long payload_end = cur + (long)box_size;

        if (memcmp(type, "trak", 4) == 0) {
            if (d->num_tracks < FAAM_MAX_TRACKS) {
                current_trak_idx = (int)d->num_tracks;
                d->num_tracks++;
                d->tracks[current_trak_idx].info.struct_size = sizeof(faam_track_info);
                memcpy(d->tracks[current_trak_idx].info.language, "und", 4);
                d->tracks[current_trak_idx].info.track_id = (uint32_t)d->num_tracks;
                d->tracks[current_trak_idx].info.track_type = FAAM_TRACK_OTHER;
                d->total_tracks++;
            } else {
                d->total_tracks++;
                cur = payload_end;
                continue;
            }
        }

        if (memcmp(type, "moov", 4) == 0 || memcmp(type, "trak", 4) == 0 ||
            memcmp(type, "mdia", 4) == 0 || memcmp(type, "minf", 4) == 0 ||
            memcmp(type, "stbl", 4) == 0 || memcmp(type, "udta", 4) == 0 ||
            memcmp(type, "meta", 4) == 0 ||
            memcmp(type, "stsd", 4) == 0 || memcmp(type, "edts", 4) == 0) {
            long sub_offset = payload_offset;
            if (memcmp(type, "meta", 4) == 0) sub_offset += 4;
            if (memcmp(type, "stsd", 4) == 0) {
                /* The first sample entry names the track's codec, known or not. */
                if (current_trak_idx >= 0 && payload_offset + 16 <= payload_end)
                    d->tracks[current_trak_idx].info.fourcc = read_u32_be(buf + payload_offset + 12);
                sub_offset += 8;
            }
            parse_boxes_recursive(buf, sub_offset, payload_end, d, current_trak_idx, tb, depth + 1);
        } else if (memcmp(type, "ilst", 4) == 0) {
            /* Tag children ("\xa9nam", "trkn", "----", ...) aren't containers
             * the generic walk above recognizes, so parse them directly
             * instead of recursing -- this is the reverse of tag.c's/mux.c's
             * ilst writers. */
            parse_ilst_children(buf, payload_offset, payload_end, d);
#ifdef FAAM_MUXER_FRAGMENTED
        } else if (memcmp(type, "mvex", 4) == 0) {
            d->fragmented = true;
            parse_boxes_recursive(buf, payload_offset, payload_end, d, current_trak_idx, tb, depth + 1);
        } else if (memcmp(type, "trex", 4) == 0 && d->num_trex < FAAM_MAX_TRACKS && payload_offset + 24 <= payload_end) {
            faam_trex *x = &d->trex[d->num_trex++];
            x->track_id = read_u32_be(buf + payload_offset + 4);
            x->duration = read_u32_be(buf + payload_offset + 12);
            x->size = read_u32_be(buf + payload_offset + 16);
            x->flags = read_u32_be(buf + payload_offset + 20);
        } else if (memcmp(type, "mehd", 4) == 0 && payload_offset + 8 <= payload_end) {
            if (buf[payload_offset] == 1) {
                if (payload_offset + 12 <= payload_end) d->mehd_duration = read_u64_be(buf + payload_offset + 4);
            } else {
                d->mehd_duration = read_u32_be(buf + payload_offset + 4);
            }
#else
        } else if (memcmp(type, "mvex", 4) == 0) {
            d->error = FAAM_ERR_NOT_BUILT; /* fragmented file, fragment support compiled out */
#endif
        } else if (memcmp(type, "chpl", 4) == 0 && payload_offset + 9 <= payload_end) {
            long p = payload_offset + 8;
            uint32_t entry_count = buf[p++];
            if (d->chapters || !entry_count) { cur = payload_end; continue; }
            d->chapters = (faam_chapter *)AllocMemory(entry_count * sizeof(faam_chapter));
            if (!d->chapters) { d->error = FAAM_ERR_INSUFFICIENT_MEM; cur = payload_end; continue; }
            for (uint32_t c = 0; c < entry_count && p + 9 <= payload_end; c++) {
                uint64_t start = read_u64_be(buf + p) / 10000;
                uint8_t tlen = buf[p + 8];
                if (p + 9 + tlen > payload_end) break;
                d->chapters[c].struct_size = sizeof(faam_chapter);
                d->chapters[c].start_ms = start;
                d->chapters[c].title = own_string(d, buf + p + 9, tlen);
                p += 9 + tlen;
                d->num_chapters++;
            }
        } else if (memcmp(type, "hdlr", 4) == 0 && current_trak_idx >= 0 && payload_offset + 12 <= payload_end) {
            if (memcmp(buf + payload_offset + 8, "soun", 4) == 0) {
                d->tracks[current_trak_idx].info.track_type = FAAM_TRACK_AUDIO;
            } else if (memcmp(buf + payload_offset + 8, "vide", 4) == 0) {
                d->tracks[current_trak_idx].info.track_type = FAAM_TRACK_VIDEO;
            }
        } else if (memcmp(type, "tkhd", 4) == 0 && current_trak_idx >= 0 && payload_offset + 80 <= payload_end) {
            uint8_t version = buf[payload_offset];
            long id_off = payload_offset + 4 + (version == 1 ? 16 : 8);
            if (id_off + 4 <= payload_end) {
                d->tracks[current_trak_idx].info.track_id = read_u32_be(buf + id_off);
            }
            /* The display matrix follows id, reserved, duration, reserved[2], layer, group, volume
             * and one more reserved field: a plain rotation by a multiple of 90 degrees reads as
             * its clockwise angle, anything else (scale, shear, flip) as 0. */
            long mat_off = id_off + 24 + (version == 1 ? 8 : 4);
            if (mat_off + 36 <= payload_end) {
                int32_t a = (int32_t)read_u32_be(buf + mat_off), b = (int32_t)read_u32_be(buf + mat_off + 4);
                int32_t c = (int32_t)read_u32_be(buf + mat_off + 12), dd = (int32_t)read_u32_be(buf + mat_off + 16);
                const int32_t one = 0x10000;
                uint16_t rot = 0;
                if (a == 0 && b == one && c == -one && dd == 0) rot = 90;
                else if (a == -one && b == 0 && c == 0 && dd == -one) rot = 180;
                else if (a == 0 && b == -one && c == one && dd == 0) rot = 270;
                d->tracks[current_trak_idx].info.rotation_degrees = rot;
            }
            long dim_off = id_off + 4 + (version == 1 ? 32 : 20) + 44;
            if (dim_off + 8 <= payload_end) {
                d->tracks[current_trak_idx].info.width = (uint16_t)(read_u32_be(buf + dim_off) >> 16);
                d->tracks[current_trak_idx].info.height = (uint16_t)(read_u32_be(buf + dim_off + 4) >> 16);
            }
        } else if (memcmp(type, "mvhd", 4) == 0 && payload_offset + 4 <= payload_end) {
            uint8_t version = buf[payload_offset];
            long ts_off = payload_offset + 4 + (version == 1 ? 16 : 8);
            if (ts_off + 4 <= payload_end) {
                d->movie_timescale = read_u32_be(buf + ts_off);
            }
        } else if (memcmp(type, "mdhd", 4) == 0 && current_trak_idx >= 0 && payload_offset + 4 <= payload_end) {
            uint8_t version = buf[payload_offset];
            long ts_off = payload_offset + 4 + (version == 1 ? 16 : 8);
            if (ts_off + 4 <= payload_end) {
                d->tracks[current_trak_idx].info.timescale = read_u32_be(buf + ts_off);
                /* The declared media length: the edit-list padding is measured against it,
                 * which can differ from the sum of the sample durations. */
                if (version == 1 && ts_off + 12 <= payload_end)
                    d->tracks[current_trak_idx].media_duration = read_u64_be(buf + ts_off + 4);
                else if (version != 1 && ts_off + 8 <= payload_end)
                    d->tracks[current_trak_idx].media_duration = read_u32_be(buf + ts_off + 4);
                long lang_off = ts_off + 4 + (version == 1 ? 8 : 4);
                if (lang_off + 2 <= payload_end) {
                    uint16_t packed = read_u16_be(buf + lang_off);
                    char *language = d->tracks[current_trak_idx].info.language;
                    for (unsigned i = 0; i < 3; i++)
                        language[i] = (char)(((packed >> (10 - i * 5)) & 31) + 0x60);
                    language[3] = '\0';
                }

            }
        } else if (memcmp(type, "mp4a", 4) == 0 && current_trak_idx >= 0) {
            d->tracks[current_trak_idx].info.codec_id = FAAM_CODEC_AAC;
            if (payload_offset + 28 <= payload_end) {
                d->tracks[current_trak_idx].info.channels = read_u16_be(buf + payload_offset + 16);
                d->tracks[current_trak_idx].info.sample_rate = read_u32_be(buf + payload_offset + 24) >> 16;
                /* QuickTime sound entries v1/v2 carry extra fields before the child boxes. */
                uint16_t qt_version = read_u16_be(buf + payload_offset + 8);
                parse_boxes_recursive(buf, payload_offset + 28 + (qt_version == 1 ? 16 : qt_version == 2 ? 36 : 0),
                                      payload_end, d, current_trak_idx, tb, depth + 1);
            }
#ifdef FAAM_MUXER_VIDEO
        } else if ((memcmp(type, "avc1", 4) == 0 || memcmp(type, "avc3", 4) == 0) && current_trak_idx >= 0) {
            d->tracks[current_trak_idx].info.codec_id = FAAM_CODEC_H264;
            if (type[3] == '3') d->tracks[current_trak_idx].info.flags |= FAAM_TRACK_INFO_INBAND_PARAMS;
            if (payload_offset + 78 <= payload_end) {
                d->tracks[current_trak_idx].info.width = read_u16_be(buf + payload_offset + 24);
                d->tracks[current_trak_idx].info.height = read_u16_be(buf + payload_offset + 26);
                parse_boxes_recursive(buf, payload_offset + 78, payload_end, d, current_trak_idx, tb, depth + 1);
            }
        } else if ((memcmp(type, "hvc1", 4) == 0 || memcmp(type, "hev1", 4) == 0) && current_trak_idx >= 0) {
            d->tracks[current_trak_idx].info.codec_id = FAAM_CODEC_H265;
            if (type[1] == 'e') d->tracks[current_trak_idx].info.flags |= FAAM_TRACK_INFO_INBAND_PARAMS;
            if (payload_offset + 78 <= payload_end) {
                d->tracks[current_trak_idx].info.width = read_u16_be(buf + payload_offset + 24);
                d->tracks[current_trak_idx].info.height = read_u16_be(buf + payload_offset + 26);
                parse_boxes_recursive(buf, payload_offset + 78, payload_end, d, current_trak_idx, tb, depth + 1);
            }
        } else if ((memcmp(type, "avcC", 4) == 0 || memcmp(type, "hvcC", 4) == 0) && current_trak_idx >= 0) {
            faam_demuxer_track *tr = &d->tracks[current_trak_idx];
            uint32_t len = (uint32_t)(payload_end - payload_offset);
            if (!tr->codec_data && len) {
                tr->codec_data = (uint8_t *)AllocMemory(len);
                if (!tr->codec_data) d->error = FAAM_ERR_INSUFFICIENT_MEM;
                else { memcpy(tr->codec_data, buf + payload_offset, len); tr->codec_data_len = len; }
            }
#endif
        } else if (memcmp(type, "esds", 4) == 0 && current_trak_idx >= 0) {
            faam_demuxer_track *tr = &d->tracks[current_trak_idx];
            const uint8_t *asc;
            uint32_t asc_len;
            if (!tr->codec_data &&
                esds_decoder_config(buf + payload_offset, (uint32_t)(payload_end - payload_offset), &asc, &asc_len) &&
                asc_len) {
                tr->codec_data = (uint8_t *)AllocMemory(asc_len);
                if (!tr->codec_data) d->error = FAAM_ERR_INSUFFICIENT_MEM;
                else { memcpy(tr->codec_data, asc, asc_len); tr->codec_data_len = asc_len; }
            }
        } else if (memcmp(type, "stts", 4) == 0 && current_trak_idx >= 0 && !tb->stts[current_trak_idx] && payload_offset + 8 <= payload_end) {
            /* The first valid table wins, as with the edit list. */
            uint32_t entries = read_u32_be(buf + payload_offset + 4);
            if (entries > 0 && entries <= (uint64_t)(payload_end - payload_offset - 8) / 8) {
                tb->num_stts[current_trak_idx] = entries;
                tb->stts[current_trak_idx] = buf + payload_offset + 8;
            }
#ifdef FAAM_MUXER_VIDEO
        } else if (memcmp(type, "stss", 4) == 0 && current_trak_idx >= 0 && !tb->stss[current_trak_idx] && payload_offset + 8 <= payload_end) {
            uint32_t entries = read_u32_be(buf + payload_offset + 4);
            if (entries > 0 && entries <= (uint64_t)(payload_end - payload_offset - 8) / 4) {
                tb->num_stss[current_trak_idx] = entries;
                tb->stss[current_trak_idx] = buf + payload_offset + 8;
            }
        } else if (memcmp(type, "ctts", 4) == 0 && current_trak_idx >= 0 && !tb->ctts[current_trak_idx] && payload_offset + 8 <= payload_end) {
            uint32_t entries = read_u32_be(buf + payload_offset + 4);
            if (entries > 0 && entries <= (uint64_t)(payload_end - payload_offset - 8) / 8) {
                tb->ctts[current_trak_idx] = buf + payload_offset + 8;
                tb->num_ctts[current_trak_idx] = entries;
            }
#endif
        } else if (memcmp(type, "elst", 4) == 0 && current_trak_idx >= 0 && !d->tracks[current_trak_idx].has_elst && payload_offset + 8 <= payload_end) {
            uint8_t version = buf[payload_offset];
            long p = payload_offset + 4;
            uint32_t entry_count = read_u32_be(buf + p);
            p += 4;
            uint32_t entry_size = version == 1 ? 20 : 12;
            for (uint32_t e = 0; e < entry_count && p + entry_size <= payload_end; e++, p += entry_size) {
                uint64_t seg_dur, media_time_raw, empty_edit_sentinel;
                if (version == 1) {
                    seg_dur = read_u64_be(buf + p);
                    media_time_raw = read_u64_be(buf + p + 8);
                    empty_edit_sentinel = 0xFFFFFFFFFFFFFFFFULL;
                } else {
                    seg_dur = read_u32_be(buf + p);
                    media_time_raw = read_u32_be(buf + p + 4);
                    empty_edit_sentinel = 0xFFFFFFFFULL;
                }
                if (media_time_raw == empty_edit_sentinel) continue;
                d->tracks[current_trak_idx].elst_segment_duration = seg_dur;
                d->tracks[current_trak_idx].elst_media_time = media_time_raw;
                d->tracks[current_trak_idx].has_elst = true;
                break;
            }
        } else if (memcmp(type, "stsz", 4) == 0 && current_trak_idx >= 0 && !tb->num_stsz[current_trak_idx] && payload_offset + 12 <= payload_end) {
            tb->fixed_size[current_trak_idx] = read_u32_be(buf + payload_offset + 4);
            uint32_t sample_count = read_u32_be(buf + payload_offset + 8);
            if (sample_count > 0 && (tb->fixed_size[current_trak_idx] || sample_count <= (uint64_t)(payload_end - payload_offset - 12) / 4) && (uint64_t)sample_count * sizeof(faam_sample) <= SIZE_MAX) {
                tb->num_stsz[current_trak_idx] = sample_count;
                if (tb->fixed_size[current_trak_idx] == 0) {
                    tb->stsz[current_trak_idx] = buf + payload_offset + 12;
                }
            }
        } else if (memcmp(type, "stsc", 4) == 0 && current_trak_idx >= 0 && !tb->stsc[current_trak_idx] && payload_offset + 8 <= payload_end) {
            uint32_t entries = read_u32_be(buf + payload_offset + 4);
            if (entries > 0 && entries <= (uint64_t)(payload_end - payload_offset - 8) / 12) {
                tb->num_stsc[current_trak_idx] = entries;
                tb->stsc[current_trak_idx] = buf + payload_offset + 8;
            }
        } else if (memcmp(type, "stco", 4) == 0 && current_trak_idx >= 0 && !tb->stco[current_trak_idx] && payload_offset + 8 <= payload_end) {
            uint32_t chunks = read_u32_be(buf + payload_offset + 4);
            if (chunks > 0 && chunks <= (uint64_t)(payload_end - payload_offset - 8) / 4 && (uint64_t)chunks * sizeof(uint64_t) <= SIZE_MAX) {
                tb->num_stco[current_trak_idx] = chunks;
                tb->stco[current_trak_idx] = buf + payload_offset + 8;
                tb->co64[current_trak_idx] = false;
            }
        } else if (memcmp(type, "co64", 4) == 0 && current_trak_idx >= 0 && !tb->stco[current_trak_idx] && payload_offset + 8 <= payload_end) {
            uint32_t chunks = read_u32_be(buf + payload_offset + 4);
            if (chunks > 0 && chunks <= (uint64_t)(payload_end - payload_offset - 8) / 8) {
                tb->num_stco[current_trak_idx] = chunks;
                tb->stco[current_trak_idx] = buf + payload_offset + 8;
                tb->co64[current_trak_idx] = true;
            }
        }

        cur = payload_end;
    }
}

/* The stream has no size call and the walk to moov stops there, so find the end by bisecting
 * for the first unreadable offset; `known` bytes are already readable. */
static uint64_t stream_extent(const faam_io *io, uint64_t known)
{
    uint8_t b;
    uint64_t lo = known, hi = known ? known * 2 : 1;
    while (hi < (1ULL << 62) && read_at(io, hi - 1, &b, 1) == 1) { lo = hi; hi *= 2; }
    while (hi - lo > 1) {
        uint64_t mid = lo + (hi - lo) / 2;
        if (read_at(io, mid - 1, &b, 1) == 1) lo = mid; else hi = mid;
    }
    return lo;
}

static faam_status faam_parse_stream(struct faam_demuxer *d, const uint8_t *buf, long moov_size)
{
    faam_trak_tables tables;
    faam_trak_tables *tb = &tables;
    memset(tb, 0, sizeof(*tb));

    parse_boxes_recursive(buf, 0, moov_size, d, -1, tb, 0);
    if (d->error) return d->error;

    /* Only the audio edit list carries priming; a video edit list (B-frame
     * delay) must not be mistaken for it. */
    for (uint32_t t = 0; t < d->num_tracks; t++) {
        if (d->tracks[t].info.track_type == FAAM_TRACK_AUDIO && d->tracks[t].has_elst) {
            d->elst_segment_duration = d->tracks[t].elst_segment_duration;
            d->elst_media_time = d->tracks[t].elst_media_time;
            d->has_elst = true;
            break;
        }
    }

    for (uint32_t t = 0; t < d->num_tracks; t++) {
        faam_demuxer_track *tr = &d->tracks[t];
        uint32_t n_samples = tb->num_stsz[t];
        if (n_samples == 0) continue;

        /* stsz can claim billions of fixed-size samples; only as many as the
         * chunk tables place in the file are real, so size the index by those. */
        uint64_t placed = 0;
        for (uint32_t c = 0; tb->stco[t] && tb->stsc[t] && c < tb->num_stco[t] && placed < n_samples; c++) {
            uint32_t per_chunk = read_u32_be(tb->stsc[t] + 4);
            for (uint32_t e = 0; e < tb->num_stsc[t] && c + 1 >= read_u32_be(tb->stsc[t] + (uint64_t)e * 12); e++)
                per_chunk = read_u32_be(tb->stsc[t] + (uint64_t)e * 12 + 4);
            placed += per_chunk;
        }
        if (placed < n_samples) n_samples = (uint32_t)placed;
        /* Equal-sized samples also have to fit in the file, however many the chunk tables
         * claim. The moov is no bound: fixed sizes are what keeps it small, so measure the
         * stream when the moov alone does not settle it. */
        if (tb->fixed_size[t] && n_samples > (uint64_t)moov_size / tb->fixed_size[t]) {
            uint64_t fit = stream_extent(&d->io, (uint64_t)moov_size) / tb->fixed_size[t];
            if (n_samples > fit) n_samples = (uint32_t)fit;
        }
        if (n_samples == 0) continue;

        tr->samples = (faam_sample *)AllocMemory(n_samples * sizeof(faam_sample));
        if (!tr->samples) { d->error = FAAM_ERR_INSUFFICIENT_MEM; return d->error; }
        memset(tr->samples, 0, n_samples * sizeof(faam_sample));
        tr->total_frames = n_samples;
        tr->info.total_frames = n_samples;

#ifdef FAAM_MUXER_VIDEO
        uint32_t sync_idx = 0;
        uint32_t ctts_entry_idx = 0;
        uint32_t ctts_run_remaining = tb->num_ctts[t] > 0 ? read_u32_be(tb->ctts[t]) : 0;
#endif
        uint32_t stts_entry_idx = 0;
        uint32_t stts_run_remaining = tb->num_stts[t] > 0 ? read_u32_be(tb->stts[t]) : 0;

        uint64_t track_tot_duration = 0;

        if (tb->stco[t] && tb->num_stco[t] > 0 && tb->stsc[t] && tb->num_stsc[t] > 0) {
            uint32_t sample_idx = 0;
            for (uint32_t chunk_idx = 0; chunk_idx < tb->num_stco[t]; chunk_idx++) {
                uint32_t chunk_num = chunk_idx + 1;
                uint64_t chunk_offset = tb->co64[t] ? read_u64_be(tb->stco[t] + (uint64_t)chunk_idx * 8)
                    : read_u32_be(tb->stco[t] + (uint64_t)chunk_idx * 4);

                uint32_t samples_in_chunk = read_u32_be(tb->stsc[t] + 4);
                for (uint32_t e = 0; e < tb->num_stsc[t]; e++) {
                    if (chunk_num >= read_u32_be(tb->stsc[t] + (uint64_t)e * 12)) {
                        samples_in_chunk = read_u32_be(tb->stsc[t] + (uint64_t)e * 12 + 4);
                    } else break;
                }

                uint64_t sample_offset_in_chunk = 0;
                for (uint32_t s = 0; s < samples_in_chunk && sample_idx < n_samples; s++) {
                    uint32_t size = (tb->fixed_size[t] != 0) ? tb->fixed_size[t] : (tb->stsz[t] ? read_u32_be(tb->stsz[t] + (uint64_t)sample_idx * 4) : 0);

                    uint32_t duration = (tr->info.track_type == FAAM_TRACK_AUDIO) ? 1024 : 3000;
                    if (tb->stts[t]) {
                        while (stts_run_remaining == 0 && stts_entry_idx + 1 < tb->num_stts[t]) {
                            stts_entry_idx++;
                            stts_run_remaining = read_u32_be(tb->stts[t] + (uint64_t)stts_entry_idx * 8);
                        }
                        if (stts_run_remaining > 0) {
                            duration = read_u32_be(tb->stts[t] + (uint64_t)stts_entry_idx * 8 + 4);
                            stts_run_remaining--;
                        }
                    }

                    bool is_keyframe = (tr->info.track_type == FAAM_TRACK_AUDIO);
#ifdef FAAM_MUXER_VIDEO
                    if (tb->num_stss[t] > 0 && tb->stss[t]) {
                        is_keyframe = false;
                        while (sync_idx < tb->num_stss[t] && read_u32_be(tb->stss[t] + (uint64_t)sync_idx * 4) < sample_idx + 1)
                            sync_idx++;
                        is_keyframe = sync_idx < tb->num_stss[t] && read_u32_be(tb->stss[t] + (uint64_t)sync_idx * 4) == sample_idx + 1;
                    }
#endif

#ifdef FAAM_MUXER_VIDEO
                    if (tb->ctts[t]) {
                        while (ctts_run_remaining == 0 && ctts_entry_idx + 1 < tb->num_ctts[t]) {
                            ctts_entry_idx++;
                            ctts_run_remaining = read_u32_be(tb->ctts[t] + (uint64_t)ctts_entry_idx * 8);
                        }
                        if (ctts_run_remaining > 0) {
                            tr->samples[sample_idx].cts_offset = (int32_t)read_u32_be(tb->ctts[t] + (uint64_t)ctts_entry_idx * 8 + 4);
                            ctts_run_remaining--;
                        }
                    }
#endif
                    tr->samples[sample_idx].offset = chunk_offset + sample_offset_in_chunk;
                    tr->samples[sample_idx].size = size;
                    if (tr->info.max_frame_bytes < size) tr->info.max_frame_bytes = size;
                    tr->samples[sample_idx].duration = duration;
                    tr->samples[sample_idx].is_keyframe = is_keyframe;

                    track_tot_duration += duration;
                    sample_offset_in_chunk += size;
                    sample_idx++;
                }
            }
        }
        tr->info.total_duration = track_tot_duration;
    }

    return d->error;
}

#ifdef FAAM_MUXER_FRAGMENTED
/* Bounds the memory one hostile or corrupt fragment can claim. */
enum { MOOF_MAX_BYTES = 16 << 20, TRUN_MAX_SAMPLES = 1 << 20 };

static faam_demuxer_track *track_by_id(faam_demuxer *d, uint32_t id)
{
    for (uint32_t t = 0; t < d->num_tracks; t++)
        if (d->tracks[t].info.track_id == id) return &d->tracks[t];
    return NULL;
}

static bool reserve_samples(faam_demuxer *d, faam_demuxer_track *tr, uint32_t need)
{
    if (need <= tr->samples_cap) return true;
    uint32_t cap = tr->samples_cap * 2 > need ? tr->samples_cap * 2 : need;
    if (cap < 64) cap = 64;
    faam_sample *p = (faam_sample *)faam_grow_memory(tr->samples, (size_t)tr->total_frames * sizeof(*tr->samples), (size_t)cap * sizeof(faam_sample));
    if (!p) { d->error = FAAM_ERR_INSUFFICIENT_MEM; return false; }
    tr->samples = p;
    tr->samples_cap = cap;
    return true;
}

/* One traf: tfhd supplies the base and defaults, each trun appends a run of
 * samples. *next_base is where a following traf starts when its tfhd names no base. */
static bool parse_traf(faam_demuxer *d, const uint8_t *b, uint32_t n, uint64_t moof_pos,
                       uint64_t *next_base, uint64_t *max_end)
{
    faam_demuxer_track *tr = NULL;
    uint32_t def_duration = 0, def_size = 0, def_flags = 0;
    uint64_t base = *next_base, cursor = *next_base;
    for (uint32_t cur = 0; cur + 8 <= n;) {
        uint32_t size = read_u32_be(b + cur);
        if (size < 8 || size > n - cur) return false;
        const uint8_t *p = b + cur + 8;
        uint32_t pn = size - 8;
        if (!memcmp(b + cur + 4, "tfhd", 4)) {
            if (pn < 8) return false;
            uint32_t flags = read_u32_be(p) & 0xFFFFFF, q = 8;
            tr = track_by_id(d, read_u32_be(p + 4));
            if (!tr) return true; /* a track the moov never declared */
            for (uint32_t x = 0; x < d->num_trex; x++) {
                if (d->trex[x].track_id == tr->info.track_id) {
                    def_duration = d->trex[x].duration;
                    def_size = d->trex[x].size;
                    def_flags = d->trex[x].flags;
                }
            }
            uint32_t need = 8 + (flags & 1 ? 8 : 0) + (flags & 2 ? 4 : 0) + (flags & 8 ? 4 : 0) +
                            (flags & 0x10 ? 4 : 0) + (flags & 0x20 ? 4 : 0);
            if (pn < need) return false;
            if (flags & 1) { base = read_u64_be(p + q); q += 8; }
            else if (flags & 0x20000) base = moof_pos;
            if (flags & 2) q += 4;
            if (flags & 8) { def_duration = read_u32_be(p + q); q += 4; }
            if (flags & 0x10) { def_size = read_u32_be(p + q); q += 4; }
            if (flags & 0x20) def_flags = read_u32_be(p + q);
            cursor = base;
        } else if (!memcmp(b + cur + 4, "trun", 4) && tr) {
            if (pn < 8) return false;
            uint32_t flags = read_u32_be(p) & 0xFFFFFF, count = read_u32_be(p + 4), q = 8;
            uint32_t first_flags = def_flags;
            if (flags & 1) {
                if (pn < q + 4) return false;
                cursor = (uint64_t)((int64_t)base + (int32_t)read_u32_be(p + q));
                q += 4;
            }
            if (flags & 4) {
                if (pn < q + 4) return false;
                first_flags = read_u32_be(p + q);
                q += 4;
            }
            uint32_t entry = 4 * (((flags >> 8) & 1) + ((flags >> 9) & 1) + ((flags >> 10) & 1) + ((flags >> 11) & 1));
            if (count > TRUN_MAX_SAMPLES || (entry && count > (pn - q) / entry)) return false;
            if (!reserve_samples(d, tr, tr->total_frames + count)) return false;
            for (uint32_t i = 0; i < count; i++) {
                uint32_t duration = def_duration, sz = def_size, sflags = i == 0 ? first_flags : def_flags;
                int32_t cts = 0;
                if (flags & 0x100) { duration = read_u32_be(p + q); q += 4; }
                if (flags & 0x200) { sz = read_u32_be(p + q); q += 4; }
                if (flags & 0x400) { sflags = read_u32_be(p + q); q += 4; }
                if (flags & 0x800) { cts = (int32_t)read_u32_be(p + q); q += 4; }
                faam_sample *sm = &tr->samples[tr->total_frames++];
                sm->offset = cursor;
                sm->size = sz;
                sm->duration = duration;
                sm->cts_offset = cts;
                sm->is_keyframe = !(sflags & 0x10000);
                if (tr->info.max_frame_bytes < sz) tr->info.max_frame_bytes = sz;
                cursor += sz;
            }
            if (cursor > *max_end) *max_end = cursor;
        }
        cur += size;
    }
    *next_base = cursor;
    return true;
}

/* Loads the next moof's samples into the per-track tables. A fragment whose
 * mdat was cut short (crash) or whose moof is damaged ends the stream, so
 * only whole fragments are ever delivered. */
static faam_status load_fragment(faam_demuxer *d)
{
    while (!d->frag_done) {
        uint8_t hdr[16];
        int32_t got = read_at(&d->io, d->frag_pos, hdr, sizeof(hdr));
        if (got < 0) { d->error = FAAM_ERR_IO_READ; break; }
        uint64_t size = got >= 8 ? read_u32_be(hdr) : 0;
        if (size == 1 && got == 16) size = read_u64_be(hdr + 8);
        if (size < 8) break;
        if (memcmp(hdr + 4, "moof", 4)) { d->frag_pos += size; continue; }
        if (size > MOOF_MAX_BYTES) break;
        uint8_t *buf = (uint8_t *)AllocMemory((size_t)size);
        if (!buf) { d->error = FAAM_ERR_INSUFFICIENT_MEM; break; }
        for (uint32_t t = 0; t < d->num_tracks; t++) d->tracks[t].total_frames = d->tracks[t].current_frame = 0;
        int32_t body = read_at(&d->io, d->frag_pos, buf, (uint32_t)size);
        if (body < 0) d->error = FAAM_ERR_IO_READ;
        bool ok = body == (int32_t)size;
        uint64_t next_base = d->frag_pos, max_end = 0;
        for (uint32_t cur = 8; ok && cur + 8 <= size;) {
            uint32_t box = read_u32_be(buf + cur);
            if (box < 8 || box > size - cur) { ok = false; break; }
            if (!memcmp(buf + cur + 4, "traf", 4))
                ok = parse_traf(d, buf + cur + 8, box - 8, d->frag_pos, &next_base, &max_end);
            cur += box;
        }
        FreeMemory(buf);
        uint8_t last;
        if (ok && !d->error && max_end) {
            int32_t tail = read_at(&d->io, max_end - 1, &last, 1);
            if (tail < 0) d->error = FAAM_ERR_IO_READ;
            ok = tail == 1;
        }
        if (!ok || d->error) break;
        d->frag_pos += size;
        for (uint32_t t = 0; t < d->num_tracks; t++)
            if (d->tracks[t].total_frames) return FAAM_OK;
    }
    d->frag_done = true;
    for (uint32_t t = 0; t < d->num_tracks; t++) d->tracks[t].total_frames = d->tracks[t].current_frame = 0;
    return FAAM_END_OF_STREAM;
}
#endif /* FAAM_MUXER_FRAGMENTED */

static bool demuxer_config_ok(const faam_demuxer_config *cfg)
{
    return !cfg || cfg->struct_size >= FAAM_DEMUXER_CONFIG_BASELINE;
}

faam_status faam_demuxer_open(const faam_demuxer_config *cfg, const faam_io *io, faam_demuxer **out_demuxer)
{
    if (!out_demuxer) return FAAM_ERR_INVALID_ARG;
    *out_demuxer = NULL;
    if (!io || !demuxer_config_ok(cfg)) return FAAM_ERR_INVALID_ARG;
    faam_io loaded;
    if (!faam_io_load(&loaded, io)) return FAAM_ERR_INVALID_ARG;
    if (!loaded.read || !loaded.seek) return FAAM_ERR_UNSUPPORTED;

    faam_demuxer *d = (faam_demuxer *)AllocMemory(sizeof(*d));
    if (!d) return FAAM_ERR_INSUFFICIENT_MEM;
    memset(d, 0, sizeof(*d));
    d->io = loaded;
    d->metadata.struct_size = sizeof(d->metadata);

    /* Walk the top-level boxes by header and keep only moov in memory;
     * mdat can be gigabytes and sample data is read on demand. */
    uint64_t pos = 0;
    for (;;) {
        uint8_t hdr[16];
        int32_t got = read_at(&d->io, pos, hdr, sizeof(hdr));
        if (got < 0) { d->error = FAAM_ERR_IO_READ; break; }
        if (got < 8) break;
        uint64_t size = read_u32_be(hdr);
        uint32_t header = 8;
        if (size == 1 && got == 16) { size = read_u64_be(hdr + 8); header = 16; }
        if (size < header) break;
        if (!memcmp(hdr + 4, "ftyp", 4) && size >= header + 4 && got >= (int32_t)header + 4)
            memcpy(d->major_brand, hdr + header, 4);
        if (!memcmp(hdr + 4, "moov", 4)) {
            if (size > MOOV_MAX_BYTES) { d->error = FAAM_ERR_INSUFFICIENT_MEM; break; }
            uint8_t *buf = (uint8_t *)AllocMemory((size_t)size);
            if (!buf) { d->error = FAAM_ERR_INSUFFICIENT_MEM; break; }
            int32_t moov_got = read_at(&d->io, pos, buf, (uint32_t)size);
            if (moov_got < 0) d->error = FAAM_ERR_IO_READ;
            else if (moov_got == (int32_t)size) {
                faam_parse_stream(d, buf, (long)size);
                /* Without iTunSMPB the edit list's media time is the priming. */
                if (!d->has_gapless && d->has_elst && d->elst_media_time < 0xFFFFFFFFULL) {
                    d->gapless.encoder_delay = (uint32_t)d->elst_media_time;
                    /* ...and whatever media runs past the edit is padding. */
                    for (uint32_t t = 0; t < d->num_tracks; t++) {
                        const faam_track_info *ti = &d->tracks[t].info;
                        if (ti->track_type != FAAM_TRACK_AUDIO || !ti->timescale || !d->movie_timescale) continue;
                        uint64_t edit = d->elst_segment_duration * ti->timescale / d->movie_timescale;
                        uint64_t used = d->elst_media_time + edit;
                        if (edit && d->tracks[t].media_duration > used)
                            d->gapless.end_padding = (uint32_t)(d->tracks[t].media_duration - used);
                        d->gapless.total_samples = edit;
                        break;
                    }
                }
#ifdef FAAM_MUXER_FRAGMENTED
                d->frag_pos = pos + size;
                if (d->fragmented && d->movie_timescale)
                    for (uint32_t t = 0; t < d->num_tracks; t++)
                        d->tracks[t].info.total_duration = d->mehd_duration * d->tracks[t].info.timescale / d->movie_timescale;
#endif
            }
            FreeMemory(buf);
            break;
        }
        pos += size;
    }

    if (d->error) {
        faam_status error = d->error;
        faam_demuxer_close(&d);
        return error;
    }
    *out_demuxer = d;
    return FAAM_OK;
}

faam_status faam_demuxer_close(faam_demuxer **handle)
{
    if (!handle || !*handle) return FAAM_OK;
    faam_demuxer *d = *handle;
    *handle = NULL;
    for (uint32_t t = 0; t < d->num_tracks; t++) {
        if (d->tracks[t].samples) FreeMemory(d->tracks[t].samples);
        FreeMemory(d->tracks[t].codec_data);
    }
    FreeMemory(d->cover_art_owned);
    FreeMemory(d->chapters);
    FreeMemory(d->custom_tags);
    while (d->strings) {
        faam_owned_string *next = d->strings->next;
        FreeMemory(d->strings);
        d->strings = next;
    }
    FreeMemory(d);
    return FAAM_OK;
}

faam_status faam_demuxer_get_num_tracks(faam_demuxer *d, uint32_t *out_held, uint32_t *out_total)
{
    if (!d || !out_held) return FAAM_ERR_INVALID_ARG;
    *out_held = d->num_tracks;
    if (out_total) *out_total = d->total_tracks;
    return FAAM_OK;
}

faam_status faam_demuxer_get_track_info(faam_demuxer *d, uint32_t track_index, faam_track_info *out_info)
{
    if (!d || !out_info || out_info->struct_size < FAAM_TRACK_INFO_BASELINE || track_index >= d->num_tracks) return FAAM_ERR_INVALID_ARG;
    faam_copy_out(out_info, out_info->struct_size, &d->tracks[track_index].info, sizeof(faam_track_info));
    return FAAM_OK;
}

faam_status faam_demuxer_get_codec_data(faam_demuxer *d, uint32_t track_id, uint8_t *out_buf, uint32_t buf_cap, uint32_t *out_len)
{
    if (!d || !out_len || (!out_buf && buf_cap)) return FAAM_ERR_INVALID_ARG;
    *out_len = 0;
    faam_demuxer_track *tr = NULL;
    for (uint32_t t = 0; t < d->num_tracks; t++) {
        if (d->tracks[t].info.track_id == track_id) {
            tr = &d->tracks[t];
            break;
        }
    }
    if (!tr) return FAAM_ERR_NO_TRACK;

    *out_len = tr->codec_data_len;
    if (tr->codec_data_len > buf_cap) return FAAM_ERR_OUTPUT_TOO_SMALL;
    if (tr->codec_data_len) memcpy(out_buf, tr->codec_data, tr->codec_data_len);
    return FAAM_OK;
}

faam_status faam_demuxer_get_major_brand(faam_demuxer *d, char out_brand[5])
{
    if (!d || !out_brand) return FAAM_ERR_INVALID_ARG;
    memcpy(out_brand, d->major_brand, 4);
    out_brand[4] = '\0';
    return FAAM_OK;
}

/* Fills *out for a track; the public wrapper copies it out to the caller's size. */
static faam_status track_gapless(faam_demuxer *d, uint32_t track_id, faam_gapless_info *out_gapless)
{
    memset(out_gapless, 0, sizeof(*out_gapless));
    out_gapless->struct_size = sizeof(*out_gapless);

    faam_demuxer_track *tr = NULL;
    for (uint32_t t = 0; t < d->num_tracks; t++) {
        if (d->tracks[t].info.track_id == track_id) {
            tr = &d->tracks[t];
            break;
        }
    }
    if (!tr) return FAAM_ERR_NO_TRACK;

    /* iTunSMPB is file-level and counts in the first audio track's units, so it
     * describes only that track; any other track has to rely on its own edit list. */
    const faam_demuxer_track *first_audio = NULL;
    for (uint32_t t = 0; t < d->num_tracks && !first_audio; t++)
        if (d->tracks[t].info.track_type == FAAM_TRACK_AUDIO) first_audio = &d->tracks[t];
    if (d->has_gapless && (tr == first_audio || !first_audio)) {
        *out_gapless = d->gapless;
        return FAAM_OK;
    }

    if (tr->has_elst && tr->elst_media_time < 0xFFFFFFFFULL) {
        uint32_t delay = (uint32_t)tr->elst_media_time;
        uint64_t edit = 0;
        if (tr->info.timescale && d->movie_timescale) {
            edit = (tr->elst_segment_duration * tr->info.timescale + d->movie_timescale / 2) / d->movie_timescale;
        }
        uint64_t used = delay + edit;
        uint32_t padding = 0;
        if (edit && tr->media_duration > used) {
            padding = (uint32_t)(tr->media_duration - used);
        }
        out_gapless->encoder_delay = delay;
        out_gapless->end_padding = padding;
        out_gapless->total_samples = edit;
    }
    return FAAM_OK;
}

faam_status faam_demuxer_get_track_gapless(faam_demuxer *d, uint32_t track_id, faam_gapless_info *out_gapless)
{
    if (!d || !out_gapless || out_gapless->struct_size < FAAM_GAPLESS_INFO_BASELINE) return FAAM_ERR_INVALID_ARG;
    if (track_id == 0) {
        /* The first audio track, else the first track. */
        for (uint32_t t = 0; t < d->num_tracks && !track_id; t++)
            if (d->tracks[t].info.track_type == FAAM_TRACK_AUDIO) track_id = d->tracks[t].info.track_id;
        if (!track_id && d->num_tracks) track_id = d->tracks[0].info.track_id;
    }
    faam_gapless_info g;
    faam_status st = track_gapless(d, track_id, &g);
    if (st == FAAM_OK || track_id == 0) {
        if (track_id == 0) memset(&g, 0, sizeof(g)), g.struct_size = sizeof(g), st = FAAM_OK;
        faam_copy_out(out_gapless, out_gapless->struct_size, &g, sizeof(g));
    }
    return st;
}

faam_status faam_demuxer_get_metadata(faam_demuxer *d, faam_metadata *out_meta)
{
    if (!d || !out_meta || out_meta->struct_size < FAAM_METADATA_BASELINE) return FAAM_ERR_INVALID_ARG;
    faam_metadata m = d->metadata;
    m.custom_tags = NULL; /* read with faam_demuxer_get_custom_tag(): the stride is the library's */
    faam_copy_out(out_meta, out_meta->struct_size, &m, sizeof(m));
    return FAAM_OK;
}

faam_status faam_demuxer_get_custom_tag(faam_demuxer *d, uint32_t index, faam_custom_tag *out_tag)
{
    if (!d || !out_tag || out_tag->struct_size < FAAM_CUSTOM_TAG_BASELINE) return FAAM_ERR_INVALID_ARG;
    if (index >= d->metadata.num_custom_tags) return FAAM_ERR_INVALID_ARG;
    faam_copy_out(out_tag, out_tag->struct_size, &d->custom_tags[index], sizeof(faam_custom_tag));
    return FAAM_OK;
}

faam_status faam_demuxer_get_num_chapters(faam_demuxer *d, uint32_t *out_count)
{
    if (!d || !out_count) return FAAM_ERR_INVALID_ARG;
    *out_count = d->num_chapters;
    return FAAM_OK;
}

faam_status faam_demuxer_get_chapter(faam_demuxer *d, uint32_t index, faam_chapter *out_chapter)
{
    if (!d || !out_chapter || out_chapter->struct_size < FAAM_CHAPTER_BASELINE || index >= d->num_chapters)
        return FAAM_ERR_INVALID_ARG;
    faam_copy_out(out_chapter, out_chapter->struct_size, &d->chapters[index], sizeof(faam_chapter));
    return FAAM_OK;
}

faam_status faam_demuxer_next_frame_loc(faam_demuxer *d, faam_frame_loc *out_loc)
{
    if (!d || !out_loc || out_loc->struct_size < FAAM_FRAME_LOC_BASELINE) return FAAM_ERR_INVALID_ARG;
    faam_demuxer_track *tr;
    for (;;) {
        uint64_t min_offset = UINT64_MAX;
        tr = NULL;
        for (uint32_t t = 0; t < d->num_tracks; t++) {
            if (d->tracks[t].current_frame < d->tracks[t].total_frames) {
                uint64_t off = d->tracks[t].samples[d->tracks[t].current_frame].offset;
                if (off < min_offset) {
                    min_offset = off;
                    tr = &d->tracks[t];
                }
            }
        }
        if (tr) break;
#ifdef FAAM_MUXER_FRAGMENTED
        if (d->fragmented && !d->frag_done && load_fragment(d) == FAAM_OK) continue;
#endif
        return d->error ? d->error : FAAM_END_OF_STREAM;
    }

    faam_frame_loc loc;
    memset(&loc, 0, sizeof(loc));
    loc.struct_size = sizeof(loc);
    loc.track_id = tr->info.track_id;
    loc.file_offset = tr->samples[tr->current_frame].offset;
    loc.frame_bytes = tr->samples[tr->current_frame].size;
    loc.duration_ticks = tr->samples[tr->current_frame].duration;
    loc.cts_offset = tr->samples[tr->current_frame].cts_offset;
    loc.is_keyframe = tr->samples[tr->current_frame].is_keyframe;
    faam_copy_out(out_loc, out_loc->struct_size, &loc, sizeof(loc));
    return FAAM_OK;
}

faam_status faam_demuxer_read_frame(faam_demuxer *d, uint8_t *out_frame, uint32_t frame_cap, uint32_t *frame_bytes)
{
    if (!d || !frame_bytes) return FAAM_ERR_INVALID_ARG;

    faam_frame_loc loc;
    memset(&loc, 0, sizeof(loc));
    loc.struct_size = sizeof(loc);
    faam_status st = faam_demuxer_next_frame_loc(d, &loc);
    if (st != FAAM_OK) return st;

    *frame_bytes = loc.frame_bytes;

    faam_demuxer_track *tr = NULL;
    for (uint32_t t = 0; t < d->num_tracks; t++) {
        if (d->tracks[t].info.track_id == loc.track_id) {
            tr = &d->tracks[t];
            break;
        }
    }
    if (!tr) return FAAM_ERR_NO_TRACK;

    if (out_frame != NULL) {
        if (loc.frame_bytes > frame_cap) return FAAM_ERR_OUTPUT_TOO_SMALL;

        if (read_at(&d->io, loc.file_offset, out_frame, loc.frame_bytes) != (int32_t)loc.frame_bytes)
            return FAAM_ERR_IO_READ;
    }

    tr->current_frame++;
    return FAAM_OK;
}

