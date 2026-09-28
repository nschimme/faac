/* Ladder Control 0 driver. Reads the fixed-layout binary intermediate that
 * parse_dump.py produces from a reference decoder's FAAD_LADDER_DUMP text
 * dump, and drives FAAC's real bitstream writer (via faacEncReemitFrame,
 * libfaac/reemit.c) one frame at a time -- no psy, no MDCT, no rate loop,
 * no FAAC-side analysis of any kind. Every syntax decision comes from the
 * reference. Output is a raw ADTS stream FAAD3 can decode directly.
 *
 * Not part of the meson build (probe-only): compile against the already
 * meson-built static lib, e.g.
 *   cc -I../../libfaac -I../../include -I../../build-ladder \
 *      reemit_main.c -o reemit_tool \
 *      ../../build-ladder/libfaac/libfaac.a -lm
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "faac.h"
#include "reemit.h"

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s <intermediate.bin> <out.aac>\n", argv[0]);
        return 1;
    }
    FILE *in = fopen(argv[1], "rb");
    if (!in) { perror("open intermediate"); return 1; }
    FILE *out = fopen(argv[2], "wb");
    if (!out) { perror("open output"); return 1; }

    faac_params p;
    faac_params_init(&p, sizeof p);
    p.sample_rate = 48000;
    p.num_channels = 2;
    p.object_type = FAAC_OBJ_LOW;
    p.output_format = FAAC_STREAM_ADTS;
    p.input_format = FAAC_INPUT_16BIT;
    p.mpeg_version = FAAC_MPEG4;
    p.bit_rate = 64000;       /* unused by the reemit path; open() just wants a valid config */
    p.rate_control = FAAC_RC_ABR;

    faac_encoder *enc = NULL;
    faac_status st = faac_encoder_open(&p, &enc);
    if (st != FAAC_OK) {
        fprintf(stderr, "faac_encoder_open failed: %d\n", st);
        return 1;
    }

    unsigned char buf[16384];
    long frames = 0, bytes = 0;
    for (;;) {
        ReemitICS ch0, ch1;
        if (feof(in)) break;
        int c = fgetc(in);
        if (c == EOF) break;
        ungetc(c, in);
        ReemitReadICS(in, &ch0);
        ReemitReadICS(in, &ch1);
        if (!ch0.present) { frames++; continue; } /* skip a hole rather than desync */

        int n = faacEncReemitFrame((faacEncHandle)enc, &ch0,
                                    ch1.present ? &ch1 : NULL, buf, sizeof buf);
        if (n < 0) {
            fprintf(stderr, "reemit_tool: faacEncReemitFrame failed at frame %ld\n", frames + 1);
            return 1;
        }
        fwrite(buf, 1, (size_t)n, out);
        bytes += n;
        frames++;
    }

    faac_encoder_close(&enc);
    fclose(in);
    fclose(out);
    fprintf(stderr, "reemit_tool: %ld frames, %ld bytes -> %s\n", frames, bytes, argv[2]);
    return 0;
}
