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

bool mp4_read_track(FILE *f, MP4Track *track)
{
    memset(track, 0, sizeof(*track));
    track->delay = 1024; /* Default priming delay */

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size < 32) return false;

    uint8_t *buf = (uint8_t *)malloc(file_size + 1);
    if (!buf) return false;
    if (fread(buf, 1, file_size, f) != (size_t)file_size) {
        free(buf);
        return false;
    }
    buf[file_size] = '\0';

    if (memcmp(buf + 4, "ftyp", 4) != 0) {
        free(buf);
        return false;
    }

    /* Extract major brand from ftyp */
    memcpy(track->major_brand, buf + 8, 4);
    track->major_brand[4] = '\0';

    uint32_t num_stsz_samples = 0;
    uint32_t *stsz_table = NULL;
    uint32_t fixed_sample_size = 0;

    STSCEntry *stsc_table = NULL;
    uint32_t num_stsc_entries = 0;

    uint32_t *stco_table = NULL;
    uint32_t num_stco_chunks = 0;

    long mdat_offset = 0;
    long mdat_size = 0;

    /* First pass: locate mdat box */
    for (long i = 0; i < file_size - 8; i++) {
        if (memcmp(buf + i + 4, "mdat", 4) == 0) {
            mdat_size = read_be32(buf + i);
            mdat_offset = i + 8;
            break;
        }
    }

    /* Second pass: parse metadata atoms outside mdat */
    for (long i = 0; i < file_size - 8; i++) {
        if (mdat_offset > 0 && i >= mdat_offset && i < mdat_offset + mdat_size) {
            continue; /* Skip audio frame payload area */
        }

        if (memcmp(buf + i, "esds", 4) == 0) {
            for (long j = i + 4; j < i + 64 && j < file_size - 4; j++) {
                if (buf[j] == 0x05) { /* AudioSpecificConfig tag */
                    uint32_t len = buf[j + 1];
                    track->asc_buf = (uint8_t *)malloc(len);
                    memcpy(track->asc_buf, buf + j + 2, len);
                    track->asc_len = len;
                    break;
                }
            }
        }

        if (memcmp(buf + i, "iTunSMPB", 8) == 0) {
            for (long j = i + 8; j < i + 128 && j < file_size - 32; j++) {
                if (memcmp(buf + j, " 00000000 ", 10) == 0) {
                    char str_buf[128] = {0};
                    memcpy(str_buf, buf + j, 100);
                    sscanf(str_buf, " %*x %x %x", &track->delay, &track->padding);
                    break;
                }
            }
        }

        if (memcmp(buf + i, "stsz", 4) == 0 && i + 20 < file_size) {
            fixed_sample_size = read_be32(buf + i + 12);
            uint32_t sample_count = read_be32(buf + i + 16);
            if (sample_count > 0 && sample_count < 1000000) {
                num_stsz_samples = sample_count;
                if (fixed_sample_size == 0) {
                    stsz_table = (uint32_t *)calloc(num_stsz_samples, sizeof(uint32_t));
                    for (uint32_t s = 0; s < num_stsz_samples && (i + 20 + s * 4) < file_size - 4; s++) {
                        stsz_table[s] = read_be32(buf + i + 20 + s * 4);
                    }
                }
            }
        }

        if (memcmp(buf + i, "stsc", 4) == 0 && i + 16 < file_size) {
            num_stsc_entries = read_be32(buf + i + 12);
            if (num_stsc_entries > 0 && num_stsc_entries < 100000) {
                stsc_table = (STSCEntry *)calloc(num_stsc_entries, sizeof(STSCEntry));
                for (uint32_t e = 0; e < num_stsc_entries && (i + 16 + e * 12) < file_size - 12; e++) {
                    stsc_table[e].first_chunk = read_be32(buf + i + 16 + e * 12);
                    stsc_table[e].samples_per_chunk = read_be32(buf + i + 16 + e * 12 + 4);
                    stsc_table[e].sample_description_index = read_be32(buf + i + 16 + e * 12 + 8);
                }
            }
        }

        if (memcmp(buf + i, "stco", 4) == 0 && i + 16 < file_size) {
            num_stco_chunks = read_be32(buf + i + 12);
            if (num_stco_chunks > 0 && num_stco_chunks < 1000000) {
                stco_table = (uint32_t *)calloc(num_stco_chunks, sizeof(uint32_t));
                for (uint32_t c = 0; c < num_stco_chunks && (i + 16 + c * 4) < file_size - 4; c++) {
                    stco_table[c] = read_be32(buf + i + 16 + c * 4);
                }
            }
        }
    }

    if (num_stsz_samples > 0) {
        track->samples = (MP4Sample *)calloc(num_stsz_samples, sizeof(MP4Sample));
        track->num_samples = num_stsz_samples;

        if (stco_table && num_stco_chunks > 0 && stsc_table && num_stsc_entries > 0) {
            uint32_t sample_idx = 0;
            for (uint32_t chunk_idx = 0; chunk_idx < num_stco_chunks; chunk_idx++) {
                uint32_t chunk_num = chunk_idx + 1;
                uint32_t chunk_offset = stco_table[chunk_idx];

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
                    track->samples[sample_idx].offset = chunk_offset + sample_offset_in_chunk;
                    track->samples[sample_idx].size = size;
                    sample_offset_in_chunk += size;
                    sample_idx++;
                }
            }
        } else if (mdat_offset > 0) {
            /* Fallback for contiguous audio frames in mdat */
            uint32_t cur = (uint32_t)mdat_offset;
            for (uint32_t s = 0; s < num_stsz_samples; s++) {
                uint32_t size = (fixed_sample_size != 0) ? fixed_sample_size : (stsz_table ? stsz_table[s] : 0);
                track->samples[s].offset = cur;
                track->samples[s].size = size;
                cur += size;
            }
        }
    }

    if (stsz_table) free(stsz_table);
    if (stsc_table) free(stsc_table);
    if (stco_table) free(stco_table);
    free(buf);

    return (track->asc_buf != NULL && track->num_samples > 0 && track->samples != NULL);
}

void mp4_free_track(MP4Track *track)
{
    if (track->asc_buf) free(track->asc_buf);
    if (track->samples) free(track->samples);
    memset(track, 0, sizeof(*track));
}
