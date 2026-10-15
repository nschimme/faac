/* Decodes 960-sample streams made by an independent encoder and compares the
 * PCM with the output of an independent decoder (FDK), after aligning the two
 * by their start-up delay. usage: test_faad_dab vector_dir */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "faad3.h"

typedef struct { const char *name; double min_snr_db; int skip_frames; } Vector;

static uint8_t *read_file(const char *dir, const char *name, const char *ext, size_t *len)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s%s", dir, name, ext);
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return NULL; }
    fseek(f, 0, SEEK_END);
    *len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*len);
    if (fread(b, 1, *len, f) != *len) { free(b); b = NULL; }
    fclose(f);
    return b;
}

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* Decode a stream of length-prefixed access units. */
static int decode(const uint8_t *d, size_t n, int16_t **pcm, size_t *samples, uint32_t *channels, uint32_t *frame_samples)
{
    uint32_t asc_len = rd32(d);
    faad_config cfg;
    faad_decoder *dec = NULL;
    faad_stream_info si = { .struct_size = sizeof si };
    faad_config_init(&cfg, sizeof cfg);
    cfg.stream_format = FAAD_STREAM_RAW;
    cfg.output_format = FAAD_OUTPUT_16BIT;
    if (faad_decoder_open(&cfg, d + 4, asc_len, &dec) != FAAD_OK) return 1;
    faad_decoder_get_info(dec, &si);
    uint8_t *out = malloc(si.max_output_bytes);
    *pcm = malloc(sizeof(int16_t) * 8 * 1920 * 40);
    *samples = 0;
    for (size_t pos = 4 + asc_len; pos + 4 <= n;) {
        uint32_t len = rd32(d + pos), used, wrote, flags;
        pos += 4;
        if (faad_decode_frame(dec, d + pos, len, &used, out, si.max_output_bytes, &wrote, &flags) != FAAD_OK) return 1;
        memcpy(*pcm + *samples, out, wrote);
        *samples += wrote / 2;
        pos += len;
    }
    faad_decoder_get_info(dec, &si);
    *channels = si.channels;
    *frame_samples = si.frame_samples;
    free(out);
    faad_decoder_close(&dec);
    return 0;
}

int main(int argc, char **argv)
{
    static const Vector vectors[] = {
        { "lc_mono32", 60.0, 2 }, { "lc_st48", 60.0, 2 }, { "he2_st48", 40.0, 3 },
        /* Nine access units; the third scored window holds frames whose fixed SBR
         * envelope borders sit at 8 and 4 8 12 of 15 slots (51.7 dB with them
         * truncated, 53.2 dB placed as FDK does). */
        { "he2_st32", 52.5, 5 },
    };
    faad_library_info library = { .struct_size = sizeof library };
    if (faad_get_library_info(&library) != FAAD_OK) return 1;
    int failed = 0;
    if (argc < 2) return 2;
    for (size_t v = 0; v < sizeof vectors / sizeof vectors[0]; v++) {
        if (v == 2 && !library.sbr_supported) {
            puts("HE vector: skipped (SBR disabled)");
            continue;
        }
        size_t n, rn, samples;
        uint32_t ch, fs;
        int16_t *pcm;
        uint8_t *d = read_file(argv[1], vectors[v].name, ".dab", &n);
        uint8_t *rb = read_file(argv[1], vectors[v].name, ".ref.pcm", &rn);
        if (!d || !rb || decode(d, n, &pcm, &samples, &ch, &fs)) { fprintf(stderr, "%s: decode failed\n", vectors[v].name); failed = 1; continue; }
        const int16_t *ref = (const int16_t *)rb;
        size_t ref_n = rn / 2, frames = samples / ch, ref_frames = ref_n / ch;
        size_t lo = (size_t)vectors[v].skip_frames * fs, hi = frames - 4096;
        double best = -1e9;
        /* the mid channel stands in for the signal: parametric stereo does not
         * reproduce a decoder's decorrelated side signal sample for sample */
        for (long lag = -4096; lag <= 4096; lag++) {
            double e = 0.0, s = 0.0;
            for (size_t i = lo; i < hi; i++) {
                long j = (long)i - lag;
                if (j < 0 || (size_t)j >= ref_frames) { e = 1e30; break; }
                double a = 0.0, b = 0.0;
                for (uint32_t c = 0; c < ch; c++) { a += pcm[i * ch + c]; b += ref[(size_t)j * ch + c]; }
                a /= ch; b /= ch;
                e += (a - b) * (a - b);
                s += a * a;
            }
            if (e < 1e30 && s > 0.0) { double snr = 10.0 * log10(s / (e + 1.0)); if (snr > best) best = snr; }
        }
        printf("%s: %.1f dB\n", vectors[v].name, best);
        if (best < vectors[v].min_snr_db) { fprintf(stderr, "%s: below %.0f dB\n", vectors[v].name, vectors[v].min_snr_db); failed = 1; }
        free(pcm); free(d); free(rb);
    }
    return failed;
}
