/*
 * FAAM - Freeware Advanced Audio/Video Muxer
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
#include "libfaam_internal.h"

typedef struct {
    uint32_t first_chunk;
    uint32_t samples_per_chunk;
    uint32_t sample_description_index;
} STSCEntry;

typedef struct {
    uint32_t sample_count;
    uint32_t sample_delta;
} STTSEntry;

/* Per-track sample tables gathered while walking moov. */
typedef struct {
    uint32_t *stsz[FAAM_MAX_TRACKS];
    uint32_t  num_stsz[FAAM_MAX_TRACKS];
    uint32_t  fixed_size[FAAM_MAX_TRACKS];
    STSCEntry *stsc[FAAM_MAX_TRACKS];
    uint32_t  num_stsc[FAAM_MAX_TRACKS];
    uint64_t *stco[FAAM_MAX_TRACKS];
    uint32_t  num_stco[FAAM_MAX_TRACKS];
    STTSEntry *stts[FAAM_MAX_TRACKS];
    uint32_t  num_stts[FAAM_MAX_TRACKS];
    uint32_t *stss[FAAM_MAX_TRACKS];
    uint32_t  num_stss[FAAM_MAX_TRACKS];
    STTSEntry *ctts[FAAM_MAX_TRACKS]; /* sample_delta carries the (signed) offset */
    uint32_t  num_ctts[FAAM_MAX_TRACKS];
} faam_trak_tables;

/* A moov this large cannot be a camera or audiobook file; refuse it before allocating. */
#define MOOV_MAX_BYTES (256U << 20)

static int32_t read_at(const faam_io *io, uint64_t pos, void *out, uint32_t n)
{
    if (!io->seek(io->user_data, pos)) return -1;
    uint8_t *p = (uint8_t *)out;
    uint32_t done = 0;
    while (done < n) {
        int32_t r = io->read(io->user_data, p + done, n - done);
        if (r <= 0) break;
        done += (uint32_t)r;
    }
    return (int32_t)done;
}

static uint32_t parse_ber_length(const uint8_t *buf, long *offset, long max_offset)
{
    uint32_t len = 0;
    int count = 0;
    while (*offset < max_offset && count < 4) {
        uint8_t b = buf[(*offset)++];
        len = (len << 7) | (b & 0x7F);
        if (!(b & 0x80)) break;
        count++;
    }
    return len;
}

static const char *own_string(faam_demuxer *d, const uint8_t *text, uint32_t len) {
    if ((size_t)len > SIZE_MAX - sizeof(faam_owned_string) - 1) {
        d->error = FAAM_ERR_INSUFFICIENT_MEM;
        return NULL;
    }
    faam_owned_string *s = (faam_owned_string *)AllocMemory(sizeof(*s) + (size_t)len + 1);
    if (!s) { d->error = FAAM_ERR_INSUFFICIENT_MEM; return NULL; }
    s->next = d->strings;
    memcpy(s->text, text, len);
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
                if (memcmp(sub_type, "name", 4) == 0 && sub_size > 12) {
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
                    faam_custom_tag *tags = (faam_custom_tag *)ReallocMemory(d->custom_tags,
                        ((size_t)count + 1) * sizeof(*tags));
                    if (!tags) d->error = FAAM_ERR_INSUFFICIENT_MEM;
                    else {
                        d->custom_tags = tags;
                        meta->custom_tags = tags;
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
            if (owned) {
                memcpy(owned, val, val_len);
                if (d->cover_art_owned) FreeMemory(d->cover_art_owned);
                d->cover_art_owned = owned;
                meta->cover_art = owned;
                meta->cover_bytes = val_len;
            }
        }

        cur += size;
    }
}
#undef FAAM_ILST_SET_STR

static void parse_boxes_recursive(const uint8_t *buf, long offset, long end, struct faam_demuxer *d,
                                  int current_trak_idx, faam_trak_tables *tb)
{
    long cur = offset;
    while (cur + 8 <= end) {
        uint64_t box_size = read_u32_be(buf + cur);
        char type[5] = {0};
        memcpy(type, buf + cur + 4, 4);

        long header_size = 8;
        if (box_size == 1 && cur + 16 <= end) {
            box_size = read_u64_be(buf + cur + 8);
            header_size = 16;
        } else if (box_size == 0) {
            box_size = end - cur;
        }

        if (box_size < (uint64_t)header_size || cur + (long)box_size > end) break;

        long payload_offset = cur + header_size;
        long payload_end = cur + (long)box_size;

        if (memcmp(type, "trak", 4) == 0) {
            if (d->num_tracks < FAAM_MAX_TRACKS) {
                current_trak_idx = (int)d->num_tracks;
                d->num_tracks++;
                d->tracks[current_trak_idx].info.struct_size = sizeof(faam_track_info);
                memcpy(d->tracks[current_trak_idx].info.language, "und", 4);
                d->tracks[current_trak_idx].info.track_id = (uint32_t)d->num_tracks;
            } else {
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
            if (memcmp(type, "stsd", 4) == 0) sub_offset += 8;
            parse_boxes_recursive(buf, sub_offset, payload_end, d, current_trak_idx, tb);
        } else if (memcmp(type, "ilst", 4) == 0) {
            /* Tag children ("\xa9nam", "trkn", "----", ...) aren't containers
             * the generic walk above recognizes, so parse them directly
             * instead of recursing -- this is the reverse of tag.c's/mux.c's
             * ilst writers. */
            parse_ilst_children(buf, payload_offset, payload_end, d);
#ifdef FAAM_MUXER_FRAGMENTED
        } else if (memcmp(type, "mvex", 4) == 0) {
            d->fragmented = true;
            parse_boxes_recursive(buf, payload_offset, payload_end, d, current_trak_idx, tb);
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
            d->error = FAAM_ERR_UNSUPPORTED; /* fragmented file, fragment support compiled out */
#endif
        } else if (memcmp(type, "chpl", 4) == 0 && payload_offset + 9 <= payload_end) {
            long p = payload_offset + 8;
            uint32_t entry_count = buf[p++];
            for (uint32_t c = 0; c < entry_count && c < 64 && p + 9 <= payload_end; c++) {
                d->chapters[c].start_ms = read_u64_be(buf + p) / 10000;
                p += 8;
                uint8_t tlen = buf[p++];
                if (p + tlen <= payload_end) {
                    d->chapters[c].title = own_string(d, buf + p, tlen);
                    p += tlen;
                }
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
                parse_boxes_recursive(buf, payload_offset + 28, payload_end, d, current_trak_idx, tb);
            }
#ifdef FAAM_MUXER_VIDEO
        } else if (memcmp(type, "avc1", 4) == 0 && current_trak_idx >= 0) {
            d->tracks[current_trak_idx].info.codec_id = FAAM_CODEC_H264;
            if (payload_offset + 78 <= payload_end) {
                d->tracks[current_trak_idx].info.width = read_u16_be(buf + payload_offset + 24);
                d->tracks[current_trak_idx].info.height = read_u16_be(buf + payload_offset + 26);
                parse_boxes_recursive(buf, payload_offset + 78, payload_end, d, current_trak_idx, tb);
            }
        } else if (memcmp(type, "hvc1", 4) == 0 && current_trak_idx >= 0) {
            d->tracks[current_trak_idx].info.codec_id = FAAM_CODEC_H265;
            if (payload_offset + 78 <= payload_end) {
                d->tracks[current_trak_idx].info.width = read_u16_be(buf + payload_offset + 24);
                d->tracks[current_trak_idx].info.height = read_u16_be(buf + payload_offset + 26);
                parse_boxes_recursive(buf, payload_offset + 78, payload_end, d, current_trak_idx, tb);
            }
        } else if ((memcmp(type, "avcC", 4) == 0 || memcmp(type, "hvcC", 4) == 0) && current_trak_idx >= 0) {
            uint32_t len = (uint32_t)(payload_end - payload_offset);
            if (len > sizeof(d->tracks[current_trak_idx].codec_data)) {
                len = sizeof(d->tracks[current_trak_idx].codec_data);
            }
            memcpy(d->tracks[current_trak_idx].codec_data, buf + payload_offset, len);
            d->tracks[current_trak_idx].codec_data_len = len;
#endif
        } else if (memcmp(type, "esds", 4) == 0 && current_trak_idx >= 0) {
            long pos = payload_offset + 4;
            while (pos < payload_end - 2) {
                uint8_t tag = buf[pos++];
                uint32_t tag_len = parse_ber_length(buf, &pos, payload_end);
                if (tag == 0x03) pos += 3;
                else if (tag == 0x04) pos += 13;
                else if (tag == 0x05) {
                    if (tag_len > 0 && pos + tag_len <= payload_end) {
                        uint32_t len = tag_len < sizeof(d->tracks[current_trak_idx].codec_data) ? tag_len : (uint32_t)sizeof(d->tracks[current_trak_idx].codec_data);
                        memcpy(d->tracks[current_trak_idx].codec_data, buf + pos, len);
                        d->tracks[current_trak_idx].codec_data_len = len;
                    }
                    break;
                } else pos += tag_len;
            }
        } else if (memcmp(type, "stts", 4) == 0 && current_trak_idx >= 0 && payload_offset + 8 <= payload_end) {
            uint32_t entries = read_u32_be(buf + payload_offset + 4);
            if (entries > 0 && entries <= (uint64_t)(payload_end - payload_offset - 8) / 8) {
                tb->num_stts[current_trak_idx] = entries;
                tb->stts[current_trak_idx] = (STTSEntry *)AllocMemory(entries * sizeof(STTSEntry));
                if (!tb->stts[current_trak_idx]) { tb->num_stts[current_trak_idx] = 0; cur = payload_end; continue; }
                for (uint32_t e = 0; e < entries; e++) {
                    tb->stts[current_trak_idx][e].sample_count = read_u32_be(buf + payload_offset + 8 + (uint64_t)e * 8);
                    tb->stts[current_trak_idx][e].sample_delta = read_u32_be(buf + payload_offset + 8 + (uint64_t)e * 8 + 4);
                }
            }
#ifdef FAAM_MUXER_VIDEO
        } else if (memcmp(type, "stss", 4) == 0 && current_trak_idx >= 0 && payload_offset + 8 <= payload_end) {
            uint32_t entries = read_u32_be(buf + payload_offset + 4);
            if (entries > 0 && entries <= (uint64_t)(payload_end - payload_offset - 8) / 4) {
                tb->num_stss[current_trak_idx] = entries;
                tb->stss[current_trak_idx] = (uint32_t *)AllocMemory(entries * sizeof(uint32_t));
                if (!tb->stss[current_trak_idx]) { tb->num_stss[current_trak_idx] = 0; cur = payload_end; continue; }
                for (uint32_t e = 0; e < entries; e++) {
                    tb->stss[current_trak_idx][e] = read_u32_be(buf + payload_offset + 8 + (uint64_t)e * 4);
                }
            }
        } else if (memcmp(type, "ctts", 4) == 0 && current_trak_idx >= 0 && payload_offset + 8 <= payload_end) {
            uint32_t entries = read_u32_be(buf + payload_offset + 4);
            if (entries > 0 && entries <= (uint64_t)(payload_end - payload_offset - 8) / 8) {
                tb->ctts[current_trak_idx] = (STTSEntry *)AllocMemory(entries * sizeof(STTSEntry));
                if (!tb->ctts[current_trak_idx]) { cur = payload_end; continue; }
                tb->num_ctts[current_trak_idx] = entries;
                for (uint32_t e = 0; e < entries; e++) {
                    tb->ctts[current_trak_idx][e].sample_count = read_u32_be(buf + payload_offset + 8 + (uint64_t)e * 8);
                    tb->ctts[current_trak_idx][e].sample_delta = read_u32_be(buf + payload_offset + 8 + (uint64_t)e * 8 + 4);
                }
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
        } else if (memcmp(type, "stsz", 4) == 0 && current_trak_idx >= 0 && payload_offset + 12 <= payload_end) {
            tb->fixed_size[current_trak_idx] = read_u32_be(buf + payload_offset + 4);
            uint32_t sample_count = read_u32_be(buf + payload_offset + 8);
            if (sample_count > 0 && (tb->fixed_size[current_trak_idx] || sample_count <= (uint64_t)(payload_end - payload_offset - 12) / 4) && (uint64_t)sample_count * sizeof(faam_sample) <= SIZE_MAX) {
                tb->num_stsz[current_trak_idx] = sample_count;
                if (tb->fixed_size[current_trak_idx] == 0) {
                    tb->stsz[current_trak_idx] = (uint32_t *)AllocMemory(sample_count * sizeof(uint32_t));
                    if (tb->stsz[current_trak_idx]) {
                        for (uint32_t s = 0; s < sample_count; s++) {
                            tb->stsz[current_trak_idx][s] = read_u32_be(buf + payload_offset + 12 + (uint64_t)s * 4);
                        }
                    }
                }
            }
        } else if (memcmp(type, "stsc", 4) == 0 && current_trak_idx >= 0 && payload_offset + 8 <= payload_end) {
            uint32_t entries = read_u32_be(buf + payload_offset + 4);
            if (entries > 0 && entries <= (uint64_t)(payload_end - payload_offset - 8) / 12) {
                tb->num_stsc[current_trak_idx] = entries;
                tb->stsc[current_trak_idx] = (STSCEntry *)AllocMemory(entries * sizeof(STSCEntry));
                if (!tb->stsc[current_trak_idx]) { tb->num_stsc[current_trak_idx] = 0; cur = payload_end; continue; }
                for (uint32_t e = 0; e < entries; e++) {
                    tb->stsc[current_trak_idx][e].first_chunk = read_u32_be(buf + payload_offset + 8 + (uint64_t)e * 12);
                    tb->stsc[current_trak_idx][e].samples_per_chunk = read_u32_be(buf + payload_offset + 8 + (uint64_t)e * 12 + 4);
                    tb->stsc[current_trak_idx][e].sample_description_index = read_u32_be(buf + payload_offset + 8 + (uint64_t)e * 12 + 8);
                }
            }
        } else if (memcmp(type, "stco", 4) == 0 && current_trak_idx >= 0 && payload_offset + 8 <= payload_end) {
            uint32_t chunks = read_u32_be(buf + payload_offset + 4);
            if (chunks > 0 && chunks <= (uint64_t)(payload_end - payload_offset - 8) / 4 && (uint64_t)chunks * sizeof(uint64_t) <= SIZE_MAX) {
                tb->num_stco[current_trak_idx] = chunks;
                tb->stco[current_trak_idx] = (uint64_t *)AllocMemory(chunks * sizeof(uint64_t));
                if (tb->stco[current_trak_idx]) {
                    for (uint32_t c = 0; c < chunks; c++) {
                        tb->stco[current_trak_idx][c] = read_u32_be(buf + payload_offset + 8 + (uint64_t)c * 4);
                    }
                }
            }
        } else if (memcmp(type, "co64", 4) == 0 && current_trak_idx >= 0 && payload_offset + 8 <= payload_end) {
            uint32_t chunks = read_u32_be(buf + payload_offset + 4);
            if (chunks > 0 && chunks <= (uint64_t)(payload_end - payload_offset - 8) / 8) {
                tb->num_stco[current_trak_idx] = chunks;
                tb->stco[current_trak_idx] = (uint64_t *)AllocMemory(chunks * sizeof(uint64_t));
                if (tb->stco[current_trak_idx]) {
                    for (uint32_t c = 0; c < chunks; c++) {
                        tb->stco[current_trak_idx][c] = read_u64_be(buf + payload_offset + 8 + (uint64_t)c * 8);
                    }
                }
            }
        }

        cur = payload_end;
    }
}

static faam_status faam_parse_stream(struct faam_demuxer *d, const uint8_t *buf, long file_size)
{
    faam_trak_tables tables;
    faam_trak_tables *tb = &tables;
    memset(tb, 0, sizeof(*tb));

    parse_boxes_recursive(buf, 0, file_size, d, -1, tb);

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

        tr->samples = (faam_sample *)AllocMemory(n_samples * sizeof(faam_sample));
        if (!tr->samples) continue;
        memset(tr->samples, 0, n_samples * sizeof(faam_sample));
        tr->total_frames = n_samples;
        tr->info.total_frames = n_samples;

#ifdef FAAM_MUXER_VIDEO
        uint32_t sync_idx = 0;
        uint32_t ctts_entry_idx = 0;
        uint32_t ctts_run_remaining = tb->num_ctts[t] > 0 ? tb->ctts[t][0].sample_count : 0;
#endif
        uint32_t stts_entry_idx = 0;
        uint32_t stts_run_remaining = tb->num_stts[t] > 0 ? tb->stts[t][0].sample_count : 0;

        uint64_t track_tot_duration = 0;

        if (tb->stco[t] && tb->num_stco[t] > 0 && tb->stsc[t] && tb->num_stsc[t] > 0) {
            uint32_t sample_idx = 0;
            for (uint32_t chunk_idx = 0; chunk_idx < tb->num_stco[t]; chunk_idx++) {
                uint32_t chunk_num = chunk_idx + 1;
                uint64_t chunk_offset = tb->stco[t][chunk_idx];

                uint32_t samples_in_chunk = tb->stsc[t][0].samples_per_chunk;
                for (uint32_t e = 0; e < tb->num_stsc[t]; e++) {
                    if (chunk_num >= tb->stsc[t][e].first_chunk) {
                        samples_in_chunk = tb->stsc[t][e].samples_per_chunk;
                    } else break;
                }

                uint64_t sample_offset_in_chunk = 0;
                for (uint32_t s = 0; s < samples_in_chunk && sample_idx < n_samples; s++) {
                    uint32_t size = (tb->fixed_size[t] != 0) ? tb->fixed_size[t] : (tb->stsz[t] ? tb->stsz[t][sample_idx] : 0);

                    uint32_t duration = (tr->info.track_type == FAAM_TRACK_AUDIO) ? 1024 : 3000;
                    if (tb->stts[t]) {
                        while (stts_run_remaining == 0 && stts_entry_idx + 1 < tb->num_stts[t]) {
                            stts_entry_idx++;
                            stts_run_remaining = tb->stts[t][stts_entry_idx].sample_count;
                        }
                        if (stts_run_remaining > 0) {
                            duration = tb->stts[t][stts_entry_idx].sample_delta;
                            stts_run_remaining--;
                        }
                    }

                    bool is_keyframe = (tr->info.track_type == FAAM_TRACK_AUDIO);
#ifdef FAAM_MUXER_VIDEO
                    if (tb->num_stss[t] > 0 && tb->stss[t]) {
                        is_keyframe = false;
                        while (sync_idx < tb->num_stss[t] && tb->stss[t][sync_idx] < sample_idx + 1)
                            sync_idx++;
                        is_keyframe = sync_idx < tb->num_stss[t] && tb->stss[t][sync_idx] == sample_idx + 1;
                    }
#endif

#ifdef FAAM_MUXER_VIDEO
                    if (tb->ctts[t]) {
                        while (ctts_run_remaining == 0 && ctts_entry_idx + 1 < tb->num_ctts[t]) {
                            ctts_entry_idx++;
                            ctts_run_remaining = tb->ctts[t][ctts_entry_idx].sample_count;
                        }
                        if (ctts_run_remaining > 0) {
                            tr->samples[sample_idx].cts_offset = (int32_t)tb->ctts[t][ctts_entry_idx].sample_delta;
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

    for (uint32_t t = 0; t < FAAM_MAX_TRACKS; t++) {
        if (tb->stsz[t]) FreeMemory(tb->stsz[t]);
        if (tb->stsc[t]) FreeMemory(tb->stsc[t]);
        if (tb->stco[t]) FreeMemory(tb->stco[t]);
        if (tb->stts[t]) FreeMemory(tb->stts[t]);
        if (tb->stss[t]) FreeMemory(tb->stss[t]);
        if (tb->ctts[t]) FreeMemory(tb->ctts[t]);
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
    faam_sample *p = (faam_sample *)ReallocMemory(tr->samples, (size_t)cap * sizeof(faam_sample));
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
        uint64_t size = got >= 8 ? read_u32_be(hdr) : 0;
        if (size == 1 && got == 16) size = read_u64_be(hdr + 8);
        if (size < 8) break;
        if (memcmp(hdr + 4, "moof", 4)) { d->frag_pos += size; continue; }
        if (size > MOOF_MAX_BYTES) break;
        uint8_t *buf = (uint8_t *)AllocMemory((size_t)size);
        if (!buf) { d->error = FAAM_ERR_INSUFFICIENT_MEM; break; }
        for (uint32_t t = 0; t < d->num_tracks; t++) d->tracks[t].total_frames = d->tracks[t].current_frame = 0;
        bool ok = read_at(&d->io, d->frag_pos, buf, (uint32_t)size) == (int32_t)size;
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
        if (!ok || d->error || (max_end && read_at(&d->io, max_end - 1, &last, 1) != 1)) break;
        d->frag_pos += size;
        for (uint32_t t = 0; t < d->num_tracks; t++)
            if (d->tracks[t].total_frames) return FAAM_OK;
    }
    d->frag_done = true;
    for (uint32_t t = 0; t < d->num_tracks; t++) d->tracks[t].total_frames = d->tracks[t].current_frame = 0;
    return FAAM_END_OF_STREAM;
}
#endif /* FAAM_MUXER_FRAGMENTED */

faam_status faam_demuxer_get_state_size(uint32_t *state_bytes)
{
    if (!state_bytes) return FAAM_ERR_INVALID_ARG;
    *state_bytes = sizeof(struct faam_demuxer);
    return FAAM_OK;
}

faam_status faam_demuxer_init(void *mem_buf, uint32_t mem_bytes, const faam_io *io, faam_demuxer **out_demuxer)
{
    if (!mem_buf || mem_bytes < sizeof(struct faam_demuxer) || !io || !out_demuxer) {
        return FAAM_ERR_INVALID_ARG;
    }

    *out_demuxer = NULL;
    struct faam_demuxer *d = (struct faam_demuxer *)mem_buf;
    memset(d, 0, sizeof(*d));
    d->io = *io;
    d->gapless.encoder_delay = 0;

    if (d->io.read && d->io.seek) {
        /* Walk the top-level boxes by header and keep only moov in memory;
         * mdat can be gigabytes and sample data is read on demand. */
        uint64_t pos = 0;
        for (;;) {
            uint8_t hdr[16];
            int32_t got = read_at(&d->io, pos, hdr, sizeof(hdr));
            if (got < 8) break;
            uint64_t size = read_u32_be(hdr);
            uint32_t header = 8;
            if (size == 1 && got == 16) { size = read_u64_be(hdr + 8); header = 16; }
            if (size < header) break;
            if (!memcmp(hdr + 4, "moov", 4)) {
                if (size > MOOV_MAX_BYTES) { d->error = FAAM_ERR_INSUFFICIENT_MEM; break; }
                uint8_t *buf = (uint8_t *)AllocMemory((size_t)size);
                if (!buf) { d->error = FAAM_ERR_INSUFFICIENT_MEM; break; }
                if (read_at(&d->io, pos, buf, (uint32_t)size) == (int32_t)size) {
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
                            if (edit && ti->total_duration > used)
                                d->gapless.end_padding = (uint32_t)(ti->total_duration - used);
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
    }

    if (d->error) {
        faam_status error = d->error;
        faam_demuxer_close(&d);
        return error;
    }
    *out_demuxer = d;
    return FAAM_OK;
}

void faam_demuxer_close(faam_demuxer **handle)
{
    if (!handle || !*handle) return;
    faam_demuxer *d = *handle;
    *handle = NULL;
    for (uint32_t t = 0; t < d->num_tracks; t++) {
        if (d->tracks[t].samples) FreeMemory(d->tracks[t].samples);
    }
    FreeMemory(d->cover_art_owned);
    FreeMemory(d->custom_tags);
    while (d->strings) {
        faam_owned_string *next = d->strings->next;
        FreeMemory(d->strings);
        d->strings = next;
    }
    if (d->heap_owned) FreeMemory(d);
}

faam_status faam_demuxer_open(const faam_io *io, faam_demuxer **out_demuxer) {
    if (!out_demuxer) return FAAM_ERR_INVALID_ARG;
    *out_demuxer = NULL;
    uint32_t size;
    faam_demuxer_get_state_size(&size);
    void *mem = AllocMemory(size);
    if (!mem) return FAAM_ERR_INSUFFICIENT_MEM;
    faam_status st = faam_demuxer_init(mem, size, io, out_demuxer);
    if (st != FAAM_OK) FreeMemory(mem);
    else (*out_demuxer)->heap_owned = true;
    return st;
}

faam_status faam_demuxer_get_num_tracks(faam_demuxer *d, uint32_t *out_num_tracks)
{
    if (!d || !out_num_tracks) return FAAM_ERR_INVALID_ARG;
    *out_num_tracks = d->num_tracks;
    return FAAM_OK;
}

faam_status faam_demuxer_get_track_info(faam_demuxer *d, uint32_t track_index, faam_track_info *out_info)
{
    if (!d || !out_info || out_info->struct_size < sizeof(*out_info) || track_index >= d->num_tracks) return FAAM_ERR_INVALID_ARG;
    *out_info = d->tracks[track_index].info;
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

faam_status faam_demuxer_get_gapless(faam_demuxer *d, faam_gapless_info *out_gapless)
{
    if (!d || !out_gapless) return FAAM_ERR_INVALID_ARG;
    *out_gapless = d->gapless;
    return FAAM_OK;
}

faam_status faam_demuxer_get_metadata(faam_demuxer *d, faam_metadata *out_meta)
{
    if (!d || !out_meta) return FAAM_ERR_INVALID_ARG;
    *out_meta = d->metadata;
    return FAAM_OK;
}

faam_status faam_demuxer_get_chapters(faam_demuxer *d, faam_chapter *out_chapters, uint32_t cap, uint32_t *out_count)
{
    if (!d || !out_count) return FAAM_ERR_INVALID_ARG;
    uint32_t count = d->num_chapters < cap ? d->num_chapters : cap;
    if (out_chapters && count > 0) {
        memcpy(out_chapters, d->chapters, count * sizeof(faam_chapter));
    }
    *out_count = d->num_chapters;
    return FAAM_OK;
}

faam_status faam_demuxer_next_frame_loc(faam_demuxer *d, faam_frame_loc *out_loc)
{
    if (!d || !out_loc) return FAAM_ERR_INVALID_ARG;
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

    out_loc->track_id = tr->info.track_id;
    out_loc->file_offset = tr->samples[tr->current_frame].offset;
    out_loc->frame_bytes = tr->samples[tr->current_frame].size;
    out_loc->duration_ticks = tr->samples[tr->current_frame].duration;
    out_loc->cts_offset = tr->samples[tr->current_frame].cts_offset;
    out_loc->is_keyframe = tr->samples[tr->current_frame].is_keyframe;
    return FAAM_OK;
}

faam_status faam_demuxer_read_frame(faam_demuxer *d, uint8_t *out_frame, uint32_t frame_cap, uint32_t *frame_bytes)
{
    if (!d || !frame_bytes) return FAAM_ERR_INVALID_ARG;

    faam_frame_loc loc;
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

        if (d->io.seek && d->io.read) {
            if (!d->io.seek(d->io.user_data, loc.file_offset)) return FAAM_ERR_IO_READ;
            int32_t r = d->io.read(d->io.user_data, out_frame, loc.frame_bytes);
            if (r != (int32_t)loc.frame_bytes) return FAAM_ERR_IO_READ;
        } else return FAAM_ERR_IO_READ;
    }

    tr->current_frame++;
    return FAAM_OK;
}

