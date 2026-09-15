/*
 * Simple MP4/M4A Atom Parser with Gapless (iTunSMPB) Support for Frontend CLI
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "faad.h"

typedef struct {
    uint32_t delay;
    uint32_t padding;
    uint64_t total_samples;
} GaplessInfo;

bool mp4_read_header(FILE *f, uint8_t **asc_buf, uint32_t *asc_len, uint32_t *delay, uint32_t *padding)
{
    if (delay) *delay = 1024; /* Default LC priming delay */
    if (padding) *padding = 0;

    fseek(f, 0, SEEK_SET);
    uint8_t b[4];
    if (fread(b, 1, 4, f) != 4) return false;
    char type[5] = {0};
    if (fread(type, 1, 4, f) != 4) return false;

    if (memcmp(type, "ftyp", 4) != 0) {
        return false;
    }

    fseek(f, 0, SEEK_SET);
    long file_size;
    fseek(f, 0, SEEK_END);
    file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *buf = (uint8_t *)malloc(file_size);
    if (!buf) return false;
    if (fread(buf, 1, file_size, f) != (size_t)file_size) {
        free(buf);
        return false;
    }

    /* Extract ASC from esds atom */
    for (long i = 0; i < file_size - 8; i++) {
        if (memcmp(buf + i, "esds", 4) == 0) {
            for (long j = i + 4; j < i + 64 && j < file_size - 4; j++) {
                if (buf[j] == 0x05) { /* AudioSpecificConfig tag */
                    uint32_t len = buf[j + 1];
                    *asc_buf = (uint8_t *)malloc(len);
                    memcpy(*asc_buf, buf + j + 2, len);
                    *asc_len = len;
                    break;
                }
            }
        }

        /* Extract iTunSMPB gapless tag if present */
        if (memcmp(buf + i, "iTunSMPB", 8) == 0) {
            for (long j = i + 8; j < i + 128 && j < file_size - 32; j++) {
                if (memcmp(buf + j, " 00000000 ", 10) == 0) {
                    uint32_t enc_delay = 0, enc_pad = 0;
                    if (sscanf((char *)buf + j, " %*x %x %x", &enc_delay, &enc_pad) == 2) {
                        if (delay) *delay = enc_delay;
                        if (padding) *padding = enc_pad;
                    }
                    break;
                }
            }
        }
    }

    free(buf);
    return (*asc_buf != NULL);
}
