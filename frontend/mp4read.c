/*
 * MP4 Atom Parser for Extracting AAC Frames and AudioSpecificConfig
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
} MP4Track;

static uint32_t read_be32_buf(const uint8_t *b)
{
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | (uint32_t)b[3];
}

bool mp4_read_track(FILE *f, MP4Track *track)
{
    memset(track, 0, sizeof(*track));

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size < 32) return false;

    uint8_t *buf = (uint8_t *)malloc(file_size);
    if (!buf) return false;
    if (fread(buf, 1, file_size, f) != (size_t)file_size) {
        free(buf);
        return false;
    }

    if (memcmp(buf + 4, "ftyp", 4) != 0) {
        free(buf);
        return false;
    }

    /* Extract esds atom for ASC */
    for (long i = 0; i < file_size - 8; i++) {
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

        /* Extract iTunSMPB gapless delay/padding */
        if (memcmp(buf + i, "iTunSMPB", 8) == 0) {
            for (long j = i + 8; j < i + 128 && j < file_size - 32; j++) {
                if (memcmp(buf + j, " 00000000 ", 10) == 0) {
                    sscanf((char *)buf + j, " %*x %x %x", &track->delay, &track->padding);
                    break;
                }
            }
        }

        /* Parse stsz atom for sample sizes */
        if (memcmp(buf + i, "stsz", 4) == 0 && i + 20 < file_size) {
            uint32_t sample_size = read_be32_buf(buf + i + 12);
            uint32_t num_samples = read_be32_buf(buf + i + 16);
            if (num_samples > 0 && num_samples < 1000000) {
                track->samples = (MP4Sample *)calloc(num_samples, sizeof(MP4Sample));
                track->num_samples = num_samples;
                if (sample_size == 0) {
                    for (uint32_t s = 0; s < num_samples && (i + 20 + s * 4) < file_size - 4; s++) {
                        track->samples[s].size = read_be32_buf(buf + i + 20 + s * 4);
                    }
                } else {
                    for (uint32_t s = 0; s < num_samples; s++) {
                        track->samples[s].size = sample_size;
                    }
                }
            }
        }

        /* Parse stco atom for 32-bit chunk offsets */
        if (memcmp(buf + i, "stco", 4) == 0 && i + 16 < file_size) {
            uint32_t num_offsets = read_be32_buf(buf + i + 12);
            if (track->samples && num_offsets > 0) {
                if (num_offsets == track->num_samples) {
                    for (uint32_t s = 0; s < num_offsets && (i + 16 + s * 4) < file_size - 4; s++) {
                        track->samples[s].offset = read_be32_buf(buf + i + 16 + s * 4);
                    }
                } else if (num_offsets == 1) {
                    /* Single mdat chunk containing all contiguous samples */
                    uint32_t base_offset = read_be32_buf(buf + i + 16);
                    uint32_t cur = base_offset;
                    for (uint32_t s = 0; s < track->num_samples; s++) {
                        track->samples[s].offset = cur;
                        cur += track->samples[s].size;
                    }
                }
            }
        }
    }

    free(buf);
    return (track->asc_buf != NULL && track->num_samples > 0 && track->samples != NULL);
}

void mp4_free_track(MP4Track *track)
{
    if (track->asc_buf) free(track->asc_buf);
    if (track->samples) free(track->samples);
    memset(track, 0, sizeof(*track));
}
