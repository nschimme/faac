/*
 * MP4 Container Box Hierarchy Parser with Chunk-to-Sample Demuxing
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "faad.h"

typedef struct {
    uint32_t offset;
    uint32_t size;
} MP4Sample;

typedef struct {
    uint8_t *asc_buf;
    uint32_t asc_len;
    uint32_t delay;
    uint32_t padding;
    MP4Sample *samples;
    uint32_t num_samples;
    char major_brand[16];
    char encoder_tag[64];
} MP4Track;

typedef struct {
    uint32_t first_chunk;
    uint32_t samples_per_chunk;
    uint32_t sample_description_index;
} STSCEntry;

static uint32_t read_be32(const uint8_t *b)
{
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | (uint32_t)b[3];
}

static uint64_t read_be64(const uint8_t *b)
{
    return ((uint64_t)b[0] << 56) | ((uint64_t)b[1] << 48) | ((uint64_t)b[2] << 40) | ((uint64_t)b[3] << 32) |
           ((uint64_t)b[4] << 24) | ((uint64_t)b[5] << 16) | ((uint64_t)b[6] << 8) | (uint64_t)b[7];
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

/* Recursive box parser walking container hierarchy */
static void parse_boxes(const uint8_t *buf, long offset, long end, MP4Track *track,
                        uint32_t **stsz_table, uint32_t *num_stsz_samples, uint32_t *fixed_sample_size,
                        STSCEntry **stsc_table, uint32_t *num_stsc_entries,
                        uint64_t **stco_table, uint32_t *num_stco_chunks)
{
    long cur = offset;
    while (cur + 8 <= end) {
        uint64_t box_size = read_be32(buf + cur);
        char type[5] = {0};
        memcpy(type, buf + cur + 4, 4);

        long header_size = 8;
        if (box_size == 1 && cur + 16 <= end) {
            box_size = read_be64(buf + cur + 8);
            header_size = 16;
        } else if (box_size == 0) {
            box_size = end - cur;
        }

        if (box_size < (uint64_t)header_size || cur + (long)box_size > end) {
            break;
        }

        long payload_offset = cur + header_size;
        long payload_end = cur + (long)box_size;

        /* Container boxes to recurse into */
        if (memcmp(type, "moov", 4) == 0 || memcmp(type, "trak", 4) == 0 ||
            memcmp(type, "mdia", 4) == 0 || memcmp(type, "minf", 4) == 0 ||
            memcmp(type, "stbl", 4) == 0 || memcmp(type, "udta", 4) == 0 ||
            memcmp(type, "meta", 4) == 0 || memcmp(type, "ilst", 4) == 0 ||
            memcmp(type, "stsd", 4) == 0 || memcmp(type, "mp4a", 4) == 0) {
            long sub_offset = payload_offset;
            if (memcmp(type, "meta", 4) == 0) sub_offset += 4; /* skip meta fullbox version */
            if (memcmp(type, "stsd", 4) == 0) sub_offset += 8; /* skip stsd header & entry count */
            if (memcmp(type, "mp4a", 4) == 0) sub_offset += 28;/* skip mp4a audio entry header */
            parse_boxes(buf, sub_offset, payload_end, track,
                        stsz_table, num_stsz_samples, fixed_sample_size,
                        stsc_table, num_stsc_entries,
                        stco_table, num_stco_chunks);
        } else if (memcmp(type, "esds", 4) == 0) {
            long pos = payload_offset + 4; /* skip version & flags */
            while (pos < payload_end - 2) {
                uint8_t tag = buf[pos++];
                uint32_t tag_len = parse_ber_length(buf, &pos, payload_end);
                if (tag == 0x03) { /* ES_Descriptor */
                    pos += 3; /* skip ES_ID + flags */
                } else if (tag == 0x04) { /* DecoderConfigDescriptor */
                    pos += 13; /* skip objectType, streamType, bufferSizeDB, maxBitrate, avgBitrate */
                } else if (tag == 0x05) { /* AudioSpecificConfig Descriptor */
                    if (tag_len > 0 && pos + tag_len <= payload_end) {
                        track->asc_buf = (uint8_t *)malloc(tag_len);
                        memcpy(track->asc_buf, buf + pos, tag_len);
                        track->asc_len = tag_len;
                    }
                    break;
                } else {
                    pos += tag_len;
                }
            }
        } else if (memcmp(type, "iTun", 4) == 0 || memcmp(type, "SMPB", 4) == 0) {
            for (long j = payload_offset; j < payload_end - 32; j++) {
                if (memcmp(buf + j, " 00000000 ", 10) == 0) {
                    char str_buf[128] = {0};
                    memcpy(str_buf, buf + j, 100);
                    sscanf(str_buf, " %*x %x %x", &track->delay, &track->padding);
                    break;
                }
            }
        } else if (memcmp(type, "stsz", 4) == 0 && payload_offset + 12 <= payload_end) {
            *fixed_sample_size = read_be32(buf + payload_offset + 4);
            uint32_t sample_count = read_be32(buf + payload_offset + 8);
            if (sample_count > 0 && sample_count < 1000000) {
                *num_stsz_samples = sample_count;
                if (*fixed_sample_size == 0) {
                    *stsz_table = (uint32_t *)calloc(sample_count, sizeof(uint32_t));
                    for (uint32_t s = 0; s < sample_count && (payload_offset + 12 + s * 4) <= payload_end - 4; s++) {
                        (*stsz_table)[s] = read_be32(buf + payload_offset + 12 + s * 4);
                    }
                }
            }
        } else if (memcmp(type, "stsc", 4) == 0 && payload_offset + 8 <= payload_end) {
            uint32_t entries = read_be32(buf + payload_offset + 4);
            if (entries > 0 && entries < 100000) {
                *num_stsc_entries = entries;
                *stsc_table = (STSCEntry *)calloc(entries, sizeof(STSCEntry));
                for (uint32_t e = 0; e < entries && (payload_offset + 8 + e * 12) <= payload_end - 12; e++) {
                    (*stsc_table)[e].first_chunk = read_be32(buf + payload_offset + 8 + e * 12);
                    (*stsc_table)[e].samples_per_chunk = read_be32(buf + payload_offset + 8 + e * 12 + 4);
                    (*stsc_table)[e].sample_description_index = read_be32(buf + payload_offset + 8 + e * 12 + 8);
                }
            }
        } else if (memcmp(type, "stco", 4) == 0 && payload_offset + 8 <= payload_end) {
            uint32_t chunks = read_be32(buf + payload_offset + 4);
            if (chunks > 0 && chunks < 1000000) {
                *num_stco_chunks = chunks;
                *stco_table = (uint64_t *)calloc(chunks, sizeof(uint64_t));
                for (uint32_t c = 0; c < chunks && (payload_offset + 8 + c * 4) <= payload_end - 4; c++) {
                    (*stco_table)[c] = read_be32(buf + payload_offset + 8 + c * 4);
                }
            }
        } else if (memcmp(type, "co64", 4) == 0 && payload_offset + 8 <= payload_end) {
            uint32_t chunks = read_be32(buf + payload_offset + 4);
            if (chunks > 0 && chunks < 1000000) {
                *num_stco_chunks = chunks;
                *stco_table = (uint64_t *)calloc(chunks, sizeof(uint64_t));
                for (uint32_t c = 0; c < chunks && (payload_offset + 8 + c * 8) <= payload_end - 8; c++) {
                    (*stco_table)[c] = read_be64(buf + payload_offset + 8 + c * 8);
                }
            }
        }

        cur = payload_end;
    }
}

bool mp4_read_track_buf(const uint8_t *buf, long file_size, MP4Track *track)
{
    memset(track, 0, sizeof(*track));
    track->delay = 1024; /* Default priming delay */

    if (file_size < 32 || !buf) return false;

    if (memcmp(buf + 4, "ftyp", 4) != 0) {
        return false;
    }

    memcpy(track->major_brand, buf + 8, 4);
    track->major_brand[4] = '\0';

    uint32_t num_stsz_samples = 0;
    uint32_t *stsz_table = NULL;
    uint32_t fixed_sample_size = 0;

    STSCEntry *stsc_table = NULL;
    uint32_t num_stsc_entries = 0;

    uint64_t *stco_table = NULL;
    uint32_t num_stco_chunks = 0;

    /* Parse MP4 box hierarchy starting from root */
    parse_boxes(buf, 0, file_size, track,
                &stsz_table, &num_stsz_samples, &fixed_sample_size,
                &stsc_table, &num_stsc_entries,
                &stco_table, &num_stco_chunks);

    if (num_stsz_samples > 0) {
        track->samples = (MP4Sample *)calloc(num_stsz_samples, sizeof(MP4Sample));
        track->num_samples = num_stsz_samples;

        if (stco_table && num_stco_chunks > 0 && stsc_table && num_stsc_entries > 0) {
            uint32_t sample_idx = 0;
            for (uint32_t chunk_idx = 0; chunk_idx < num_stco_chunks; chunk_idx++) {
                uint32_t chunk_num = chunk_idx + 1;
                uint64_t chunk_offset = stco_table[chunk_idx];

                uint32_t samples_in_chunk = stsc_table[0].samples_per_chunk;
                for (uint32_t e = 0; e < num_stsc_entries; e++) {
                    if (chunk_num >= stsc_table[e].first_chunk) {
                        samples_in_chunk = stsc_table[e].samples_per_chunk;
                    } else {
                        break;
                    }
                }

                uint32_t sample_offset_in_chunk = 0;
                for (uint32_t s = 0; s < samples_in_chunk && sample_idx < num_stsz_samples; s++) {
                    uint32_t size = (fixed_sample_size != 0) ? fixed_sample_size : (stsz_table ? stsz_table[sample_idx] : 0);
                    track->samples[sample_idx].offset = (uint32_t)(chunk_offset + sample_offset_in_chunk);
                    track->samples[sample_idx].size = size;
                    sample_offset_in_chunk += size;
                    sample_idx++;
                }
            }
        }
    }

    if (stsz_table) free(stsz_table);
    if (stsc_table) free(stsc_table);
    if (stco_table) free(stco_table);

    return (track->asc_buf != NULL && track->num_samples > 0 && track->samples != NULL);
}

void mp4_free_track(MP4Track *track)
{
    if (track->asc_buf) free(track->asc_buf);
    if (track->samples) free(track->samples);
    memset(track, 0, sizeof(*track));
}
