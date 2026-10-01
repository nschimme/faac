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
                                  int current_trak_idx,
                                  uint32_t **stsz_tables, uint32_t *num_stsz_samples, uint32_t *fixed_sample_sizes,
                                  STSCEntry **stsc_tables, uint32_t *num_stsc_entries,
                                  uint64_t **stco_tables, uint32_t *num_stco_chunks,
                                  STTSEntry **stts_tables, uint32_t *num_stts_entries,
                                  uint32_t **stss_tables, uint32_t *num_stss_entries)
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
            parse_boxes_recursive(buf, sub_offset, payload_end, d, current_trak_idx,
                                   stsz_tables, num_stsz_samples, fixed_sample_sizes,
                                   stsc_tables, num_stsc_entries,
                                   stco_tables, num_stco_chunks,
                                   stts_tables, num_stts_entries,
                                   stss_tables, num_stss_entries);
        } else if (memcmp(type, "ilst", 4) == 0) {
            /* Tag children ("\xa9nam", "trkn", "----", ...) aren't containers
             * the generic walk above recognizes, so parse them directly
             * instead of recursing -- this is the reverse of tag.c's/mux.c's
             * ilst writers. */
            parse_ilst_children(buf, payload_offset, payload_end, d);
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
                parse_boxes_recursive(buf, payload_offset + 28, payload_end, d, current_trak_idx,
                                       stsz_tables, num_stsz_samples, fixed_sample_sizes,
                                       stsc_tables, num_stsc_entries,
                                       stco_tables, num_stco_chunks,
                                       stts_tables, num_stts_entries,
                                       stss_tables, num_stss_entries);
            }
        } else if (memcmp(type, "avc1", 4) == 0 && current_trak_idx >= 0) {
            d->tracks[current_trak_idx].info.codec_id = FAAM_CODEC_H264;
            if (payload_offset + 78 <= payload_end) {
                d->tracks[current_trak_idx].info.width = read_u16_be(buf + payload_offset + 24);
                d->tracks[current_trak_idx].info.height = read_u16_be(buf + payload_offset + 26);
                parse_boxes_recursive(buf, payload_offset + 78, payload_end, d, current_trak_idx,
                                       stsz_tables, num_stsz_samples, fixed_sample_sizes,
                                       stsc_tables, num_stsc_entries,
                                       stco_tables, num_stco_chunks,
                                       stts_tables, num_stts_entries,
                                       stss_tables, num_stss_entries);
            }
        } else if (memcmp(type, "hvc1", 4) == 0 && current_trak_idx >= 0) {
            d->tracks[current_trak_idx].info.codec_id = FAAM_CODEC_H265;
            if (payload_offset + 78 <= payload_end) {
                d->tracks[current_trak_idx].info.width = read_u16_be(buf + payload_offset + 24);
                d->tracks[current_trak_idx].info.height = read_u16_be(buf + payload_offset + 26);
                parse_boxes_recursive(buf, payload_offset + 78, payload_end, d, current_trak_idx,
                                       stsz_tables, num_stsz_samples, fixed_sample_sizes,
                                       stsc_tables, num_stsc_entries,
                                       stco_tables, num_stco_chunks,
                                       stts_tables, num_stts_entries,
                                       stss_tables, num_stss_entries);
            }
        } else if ((memcmp(type, "avcC", 4) == 0 || memcmp(type, "hvcC", 4) == 0) && current_trak_idx >= 0) {
            uint32_t len = (uint32_t)(payload_end - payload_offset);
            if (len > sizeof(d->tracks[current_trak_idx].codec_data)) {
                len = sizeof(d->tracks[current_trak_idx].codec_data);
            }
            memcpy(d->tracks[current_trak_idx].codec_data, buf + payload_offset, len);
            d->tracks[current_trak_idx].codec_data_len = len;
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
                num_stts_entries[current_trak_idx] = entries;
                stts_tables[current_trak_idx] = (STTSEntry *)AllocMemory(entries * sizeof(STTSEntry));
                if (!stts_tables[current_trak_idx]) { num_stts_entries[current_trak_idx] = 0; cur = payload_end; continue; }
                for (uint32_t e = 0; e < entries; e++) {
                    stts_tables[current_trak_idx][e].sample_count = read_u32_be(buf + payload_offset + 8 + (uint64_t)e * 8);
                    stts_tables[current_trak_idx][e].sample_delta = read_u32_be(buf + payload_offset + 8 + (uint64_t)e * 8 + 4);
                }
            }
        } else if (memcmp(type, "stss", 4) == 0 && current_trak_idx >= 0 && payload_offset + 8 <= payload_end) {
            uint32_t entries = read_u32_be(buf + payload_offset + 4);
            if (entries > 0 && entries <= (uint64_t)(payload_end - payload_offset - 8) / 4) {
                num_stss_entries[current_trak_idx] = entries;
                stss_tables[current_trak_idx] = (uint32_t *)AllocMemory(entries * sizeof(uint32_t));
                if (!stss_tables[current_trak_idx]) { num_stss_entries[current_trak_idx] = 0; cur = payload_end; continue; }
                for (uint32_t e = 0; e < entries; e++) {
                    stss_tables[current_trak_idx][e] = read_u32_be(buf + payload_offset + 8 + (uint64_t)e * 4);
                }
            }
        } else if (memcmp(type, "elst", 4) == 0 && !d->has_elst && payload_offset + 8 <= payload_end) {
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
                d->elst_segment_duration = seg_dur;
                d->elst_media_time = media_time_raw;
                d->has_elst = true;
                break;
            }
        } else if (memcmp(type, "stsz", 4) == 0 && current_trak_idx >= 0 && payload_offset + 12 <= payload_end) {
            fixed_sample_sizes[current_trak_idx] = read_u32_be(buf + payload_offset + 4);
            uint32_t sample_count = read_u32_be(buf + payload_offset + 8);
            if (sample_count > 0 && (fixed_sample_sizes[current_trak_idx] || sample_count <= (uint64_t)(payload_end - payload_offset - 12) / 4) && (uint64_t)sample_count * sizeof(faam_sample) <= SIZE_MAX) {
                num_stsz_samples[current_trak_idx] = sample_count;
                if (fixed_sample_sizes[current_trak_idx] == 0) {
                    stsz_tables[current_trak_idx] = (uint32_t *)AllocMemory(sample_count * sizeof(uint32_t));
                    if (stsz_tables[current_trak_idx]) {
                        for (uint32_t s = 0; s < sample_count; s++) {
                            stsz_tables[current_trak_idx][s] = read_u32_be(buf + payload_offset + 12 + (uint64_t)s * 4);
                        }
                    }
                }
            }
        } else if (memcmp(type, "stsc", 4) == 0 && current_trak_idx >= 0 && payload_offset + 8 <= payload_end) {
            uint32_t entries = read_u32_be(buf + payload_offset + 4);
            if (entries > 0 && entries <= (uint64_t)(payload_end - payload_offset - 8) / 12) {
                num_stsc_entries[current_trak_idx] = entries;
                stsc_tables[current_trak_idx] = (STSCEntry *)AllocMemory(entries * sizeof(STSCEntry));
                if (!stsc_tables[current_trak_idx]) { num_stsc_entries[current_trak_idx] = 0; cur = payload_end; continue; }
                for (uint32_t e = 0; e < entries; e++) {
                    stsc_tables[current_trak_idx][e].first_chunk = read_u32_be(buf + payload_offset + 8 + (uint64_t)e * 12);
                    stsc_tables[current_trak_idx][e].samples_per_chunk = read_u32_be(buf + payload_offset + 8 + (uint64_t)e * 12 + 4);
                    stsc_tables[current_trak_idx][e].sample_description_index = read_u32_be(buf + payload_offset + 8 + (uint64_t)e * 12 + 8);
                }
            }
        } else if (memcmp(type, "stco", 4) == 0 && current_trak_idx >= 0 && payload_offset + 8 <= payload_end) {
            uint32_t chunks = read_u32_be(buf + payload_offset + 4);
            if (chunks > 0 && chunks <= (uint64_t)(payload_end - payload_offset - 8) / 4 && (uint64_t)chunks * sizeof(uint64_t) <= SIZE_MAX) {
                num_stco_chunks[current_trak_idx] = chunks;
                stco_tables[current_trak_idx] = (uint64_t *)AllocMemory(chunks * sizeof(uint64_t));
                if (stco_tables[current_trak_idx]) {
                    for (uint32_t c = 0; c < chunks; c++) {
                        stco_tables[current_trak_idx][c] = read_u32_be(buf + payload_offset + 8 + (uint64_t)c * 4);
                    }
                }
            }
        } else if (memcmp(type, "co64", 4) == 0 && current_trak_idx >= 0 && payload_offset + 8 <= payload_end) {
            uint32_t chunks = read_u32_be(buf + payload_offset + 4);
            if (chunks > 0 && chunks <= (uint64_t)(payload_end - payload_offset - 8) / 8) {
                num_stco_chunks[current_trak_idx] = chunks;
                stco_tables[current_trak_idx] = (uint64_t *)AllocMemory(chunks * sizeof(uint64_t));
                if (stco_tables[current_trak_idx]) {
                    for (uint32_t c = 0; c < chunks; c++) {
                        stco_tables[current_trak_idx][c] = read_u64_be(buf + payload_offset + 8 + (uint64_t)c * 8);
                    }
                }
            }
        }

        cur = payload_end;
    }
}

static faam_status faam_parse_stream(struct faam_demuxer *d, const uint8_t *buf, long file_size)
{
    uint32_t num_stsz_samples[FAAM_MAX_TRACKS] = {0};
    uint32_t *stsz_tables[FAAM_MAX_TRACKS] = {0};
    uint32_t fixed_sample_sizes[FAAM_MAX_TRACKS] = {0};

    STSCEntry *stsc_tables[FAAM_MAX_TRACKS] = {0};
    uint32_t num_stsc_entries[FAAM_MAX_TRACKS] = {0};

    uint64_t *stco_tables[FAAM_MAX_TRACKS] = {0};
    uint32_t num_stco_chunks[FAAM_MAX_TRACKS] = {0};

    STTSEntry *stts_tables[FAAM_MAX_TRACKS] = {0};
    uint32_t num_stts_entries[FAAM_MAX_TRACKS] = {0};

    uint32_t *stss_tables[FAAM_MAX_TRACKS] = {0};
    uint32_t num_stss_entries[FAAM_MAX_TRACKS] = {0};

    parse_boxes_recursive(buf, 0, file_size, d, -1,
                           stsz_tables, num_stsz_samples, fixed_sample_sizes,
                           stsc_tables, num_stsc_entries,
                           stco_tables, num_stco_chunks,
                           stts_tables, num_stts_entries,
                           stss_tables, num_stss_entries);

    for (uint32_t t = 0; t < d->num_tracks; t++) {
        faam_demuxer_track *tr = &d->tracks[t];
        uint32_t n_samples = num_stsz_samples[t];
        if (n_samples == 0) continue;

        tr->samples = (faam_sample *)AllocMemory(n_samples * sizeof(faam_sample));
        if (!tr->samples) continue;
        memset(tr->samples, 0, n_samples * sizeof(faam_sample));
        tr->total_frames = n_samples;
        tr->info.total_frames = n_samples;

        uint32_t sync_idx = 0;
        uint32_t stts_entry_idx = 0;
        uint32_t stts_run_remaining = num_stts_entries[t] > 0 ? stts_tables[t][0].sample_count : 0;

        uint64_t track_tot_duration = 0;

        if (stco_tables[t] && num_stco_chunks[t] > 0 && stsc_tables[t] && num_stsc_entries[t] > 0) {
            uint32_t sample_idx = 0;
            for (uint32_t chunk_idx = 0; chunk_idx < num_stco_chunks[t]; chunk_idx++) {
                uint32_t chunk_num = chunk_idx + 1;
                uint64_t chunk_offset = stco_tables[t][chunk_idx];

                uint32_t samples_in_chunk = stsc_tables[t][0].samples_per_chunk;
                for (uint32_t e = 0; e < num_stsc_entries[t]; e++) {
                    if (chunk_num >= stsc_tables[t][e].first_chunk) {
                        samples_in_chunk = stsc_tables[t][e].samples_per_chunk;
                    } else break;
                }

                uint64_t sample_offset_in_chunk = 0;
                for (uint32_t s = 0; s < samples_in_chunk && sample_idx < n_samples; s++) {
                    uint32_t size = (fixed_sample_sizes[t] != 0) ? fixed_sample_sizes[t] : (stsz_tables[t] ? stsz_tables[t][sample_idx] : 0);

                    uint32_t duration = (tr->info.track_type == FAAM_TRACK_AUDIO) ? 1024 : 3000;
                    if (stts_tables[t]) {
                        while (stts_run_remaining == 0 && stts_entry_idx + 1 < num_stts_entries[t]) {
                            stts_entry_idx++;
                            stts_run_remaining = stts_tables[t][stts_entry_idx].sample_count;
                        }
                        if (stts_run_remaining > 0) {
                            duration = stts_tables[t][stts_entry_idx].sample_delta;
                            stts_run_remaining--;
                        }
                    }

                    bool is_keyframe = (tr->info.track_type == FAAM_TRACK_AUDIO);
                    if (num_stss_entries[t] > 0 && stss_tables[t]) {
                        is_keyframe = false;
                        while (sync_idx < num_stss_entries[t] && stss_tables[t][sync_idx] < sample_idx + 1)
                            sync_idx++;
                        is_keyframe = sync_idx < num_stss_entries[t] && stss_tables[t][sync_idx] == sample_idx + 1;
                    }

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
        if (stsz_tables[t]) FreeMemory(stsz_tables[t]);
        if (stsc_tables[t]) FreeMemory(stsc_tables[t]);
        if (stco_tables[t]) FreeMemory(stco_tables[t]);
        if (stts_tables[t]) FreeMemory(stts_tables[t]);
        if (stss_tables[t]) FreeMemory(stss_tables[t]);
    }

    return d->error;
}

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

    if (d->io.read) {
        if (d->io.seek) d->io.seek(d->io.user_data, 0);

        size_t buf_cap = 65536;
        size_t buf_len = 0;
        uint8_t *buf = (uint8_t *)AllocMemory(buf_cap);
        if (buf) {
            int32_t r = 0;
            while (1) {
                if (buf_len >= buf_cap) {
                    size_t new_cap = buf_cap * 2;
                    uint8_t *nb = (uint8_t *)ReallocMemory(buf, new_cap);
                    if (!nb) break;
                    buf = nb;
                    buf_cap = new_cap;
                }
                r = d->io.read(d->io.user_data, buf + buf_len, (uint32_t)(buf_cap - buf_len));
                if (r <= 0) break;
                buf_len += r;
            }
            if (buf_len > 32) {
                faam_parse_stream(d, buf, (long)buf_len);
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
            }
            FreeMemory(buf);
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
    faam_demuxer_track *tr = NULL;
    uint64_t min_offset = UINT64_MAX;

    for (uint32_t t = 0; t < d->num_tracks; t++) {
        if (d->tracks[t].current_frame < d->tracks[t].total_frames) {
            uint64_t off = d->tracks[t].samples[d->tracks[t].current_frame].offset;
            if (off < min_offset) {
                min_offset = off;
                tr = &d->tracks[t];
            }
        }
    }
    if (!tr) return FAAM_END_OF_STREAM;

    out_loc->track_id = tr->info.track_id;
    out_loc->file_offset = tr->samples[tr->current_frame].offset;
    out_loc->frame_bytes = tr->samples[tr->current_frame].size;
    out_loc->duration_ticks = tr->samples[tr->current_frame].duration;
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

