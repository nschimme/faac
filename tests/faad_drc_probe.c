/* Binary frame records keep Python assertions on the public API and PCM. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
/* The window tag proves that the transient fixture exercises short blocks. */
#include "faad_internal.h"

int main(int argc, char **argv)
{
    if (argc != 7 && argc != 8) return 2;
    faad_config cfg;
    faad_config_init(&cfg, sizeof cfg);
    cfg.drc_cut = atoi(argv[2]);
    cfg.drc_boost = atoi(argv[3]);
    cfg.drc_target_level = atoi(argv[4]);
    cfg.output_format = atoi(argv[5]) ? FAAD_OUTPUT_FLOAT : FAAD_OUTPUT_16BIT;
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    uint8_t asc[256];
    uint32_t asc_size = 0;
    if (argc == 8) {
        uint8_t length[4];
        if (fread(length, 1, 4, f) != 4) return 2;
        asc_size = length[0] | length[1] << 8 | length[2] << 16 | (uint32_t)length[3] << 24;
        if (asc_size > sizeof asc || fread(asc, 1, asc_size, f) != asc_size) return 2;
        cfg.stream_format = FAAD_STREAM_RAW;
    }
    faad_decoder *dec = NULL;
    faad_status status = faad_decoder_open(&cfg, asc_size ? asc : NULL, asc_size, &dec);
    if (status != FAAD_OK) { fclose(f); return status == FAAD_ERR_INVALID_ARGUMENT ? 3 : 2; }
    faad_stream_info info = { .struct_size = sizeof info };
    faad_decoder_get_info(dec, &info);
    uint8_t *pcm = malloc(info.max_output_bytes + 32);
    if (!pcm) return 2;
    memset(pcm + info.max_output_bytes, 0xa5, 32);
    uint8_t packet[8192];
    unsigned frame = 0;
    while (1) {
        unsigned size;
        if (argc == 8) {
            uint8_t length[4];
            if (fread(length, 1, 4, f) != 4) break;
            size = length[0] | length[1] << 8 | length[2] << 16 | (uint32_t)length[3] << 24;
            if (size > sizeof packet || fread(packet, 1, size, f) != size) return 2;
        } else {
            if (fread(packet, 1, 7, f) != 7) break;
            size = ((packet[3] & 3) << 11) | (packet[4] << 3) | (packet[5] >> 5);
            if (size < 7 || fread(packet + 7, 1, size - 7, f) != size - 7) return 2;
        }
        uint32_t used, wrote, flags;
        status = faad_decode_frame(dec, packet, size, &used, pcm, info.max_output_bytes, &wrote, &flags);
        for (unsigned i = 0; i < 32; i++)
            if (pcm[info.max_output_bytes + i] != 0xa5) return 5;
        faad_decoder_get_info(dec, &info);
        uint32_t record[] = { (uint32_t)status, used, wrote, flags,
                             info.prog_ref_level_present, info.prog_ref_level,
                             dec->win_seq[0] == EIGHT_SHORT_SEQUENCE };
        fwrite(record, sizeof record, 1, stdout);
        fwrite(pcm, 1, wrote, stdout);
        if (++frame == (unsigned)atoi(argv[6])) {
            faad_decoder_flush(dec);
            faad_decoder_get_info(dec, &info);
            if (info.prog_ref_level_present) return 4;
        }
    }
    free(pcm);
    faad_decoder_close(&dec);
    fclose(f);
    return 0;
}
