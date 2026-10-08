/*
 * libFuzzer harness for libfaad's ADTS and RAW decoder API.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "faad.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 2)
        return 0;

    faad_config cfg;
    faad_config_init(&cfg, sizeof(cfg));

    uint8_t mode = data[0];
    const uint8_t *payload = data + 1;
    size_t payload_len = size - 1;

    cfg.stream_format = (mode & 1) ? FAAD_STREAM_ADTS : FAAD_STREAM_RAW;
    cfg.output_format = (enum faad_output_format)(1 + ((mode >> 1) & 3)); /* 16BIT, 24BIT, 32BIT, FLOAT */
    cfg.downmix_mode  = (enum faad_downmix_mode)((mode >> 3) & 3);

    faad_decoder *dec = NULL;
    void *allocated_mem = NULL;
    faad_status st;

    /* Test both heap open and static init */
    if (mode & 0x20) {
        uint32_t state_bytes = 0;
        if (faad_get_state_size(&state_bytes) == FAAD_OK && state_bytes > 0) {
            allocated_mem = malloc(state_bytes);
            if (allocated_mem) {
                st = faad_decoder_init(allocated_mem, state_bytes, &cfg, payload, (uint32_t)(payload_len < 64 ? payload_len : 64), &dec);
                if (st != FAAD_OK) {
                    free(allocated_mem);
                    allocated_mem = NULL;
                    dec = NULL;
                }
            }
        }
    } else {
        st = faad_decoder_open(&cfg, payload, (uint32_t)(payload_len < 64 ? payload_len : 64), &dec);
        if (st != FAAD_OK) {
            dec = NULL;
        }
    }

    if (!dec) {
        /* Try init/open without ASC bytes */
        if (faad_decoder_open(&cfg, NULL, 0, &dec) != FAAD_OK) {
            free(allocated_mem);
            return 0;
        }
    }

    faad_stream_info info;
    info.struct_size = sizeof(info);
    (void)faad_decoder_get_info(dec, &info);

    uint8_t pcm[16384];
    size_t pos = 0;
    int frames = 0;

    while (pos < payload_len && frames < 128) {
        uint32_t consumed = 0;
        uint32_t written = 0;
        uint32_t flags = 0;

        st = faad_decode_frame(dec, payload + pos, (uint32_t)(payload_len - pos),
                               &consumed, pcm, sizeof(pcm), &written, &flags);

        if (flags & FAAD_FRAME_FORMAT_CHANGED) {
            info.struct_size = sizeof(info);
            (void)faad_decoder_get_info(dec, &info);
        }

        if (st != FAAD_OK && st != FAAD_ERR_NEED_MORE_DATA && st != FAAD_ERR_SYNC_LOST && st != FAAD_ERR_DECODE_FAILED) {
            break;
        }

        if (consumed == 0) {
            pos++;
        } else {
            pos += consumed;
        }
        frames++;

        if (frames % 32 == 0) {
            (void)faad_decoder_flush(dec);
        }
    }

    faad_decoder_close(&dec);
    free(allocated_mem);
    return 0;
}
