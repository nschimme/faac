/*
 * faar - Polyphase Farrow-Interpolated Kaiser-Sinc Audio Resampler CLI Tool
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#ifdef HAVE_GETOPT_H
# include <getopt.h>
#else
# include "getopt.h"
# include "getopt.c"
#endif
#include <faar.h>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

typedef enum {
    FMT_F32LE = 0,
    FMT_S16LE = 1,
    FMT_S24LE = 2
} pcm_format_t;

static void show_help(void) {
    printf(
        "FAAR - Freeware Advanced Audio Resampler\n"
        "Usage: faar [options] <input> <output>\n\n"
        "Core Options:\n"
        "  -r, --rate <Hz>        Target sample rate (Required, e.g., 48000, 44100)\n"
        "  -q, --quality <preset> DSP filter quality (fast, good, high) [default: good]\n"
        "  -c, --channels <num>   Audio channel count [default: 2]\n"
        "  -f, --format <fmt>     Raw PCM sample format (f32le, s16le, s24le) [default: f32le]\n"
        "  -d, --dither <type>    Dithering type for integer format output (none, tpdf, shaped) [default: none]\n"
        "  -o, --output <path>    Explicit output path (or use - for stdout)\n"
        "  -h, --help             Display this help guide\n\n"
        "Examples:\n"
        "  faar -r 48000 input_96k.wav output_48k.wav\n"
        "  faar -r 48000 -q high -d tpdf -f s16le input.wav output.wav\n"
        "  cat input.raw | faar -r 48000 -c 2 -f f32le - - | aplay -f S16_LE -r 48000\n"
    );
}

static bool parse_wav_header(FILE *f, uint32_t *rate, uint16_t *channels, uint16_t *bits, pcm_format_t *fmt, uint32_t *data_size) {
    uint8_t hdr[44];
    if (fread(hdr, 1, 44, f) != 44) return false;
    if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) return false;

    uint16_t audio_fmt = hdr[20] | (hdr[21] << 8);
    *channels = hdr[22] | (hdr[23] << 8);
    *rate = hdr[24] | (hdr[25] << 8) | (hdr[26] << 16) | (hdr[27] << 24);
    *bits = hdr[34] | (hdr[35] << 8);

    if (audio_fmt == 3 && *bits == 32) *fmt = FMT_F32LE;
    else if (audio_fmt == 1 && *bits == 16) *fmt = FMT_S16LE;
    else if (audio_fmt == 1 && *bits == 24) *fmt = FMT_S24LE;
    else return false;

    *data_size = hdr[40] | (hdr[41] << 8) | (hdr[42] << 16) | (hdr[43] << 24);
    return true;
}

static void write_wav_header(FILE *f, uint32_t rate, uint16_t channels, pcm_format_t fmt, uint32_t data_size) {
    uint8_t hdr[44] = {0};
    uint16_t bits = (fmt == FMT_F32LE) ? 32 : (fmt == FMT_S24LE ? 24 : 16);
    uint16_t audio_fmt = (fmt == FMT_F32LE) ? 3 : 1;
    uint32_t block_align = channels * (bits / 8);
    uint32_t byte_rate = rate * block_align;
    uint32_t riff_size = data_size + 36;

    memcpy(hdr, "RIFF", 4);
    hdr[4] = riff_size & 0xFF; hdr[5] = (riff_size >> 8) & 0xFF;
    hdr[6] = (riff_size >> 16) & 0xFF; hdr[7] = (riff_size >> 24) & 0xFF;
    memcpy(hdr + 8, "WAVEfmt ", 8);
    hdr[16] = 16; /* Subchunk1Size */
    hdr[20] = audio_fmt & 0xFF; hdr[21] = (audio_fmt >> 8) & 0xFF;
    hdr[22] = channels & 0xFF; hdr[23] = (channels >> 8) & 0xFF;
    hdr[24] = rate & 0xFF; hdr[25] = (rate >> 8) & 0xFF;
    hdr[26] = (rate >> 16) & 0xFF; hdr[27] = (rate >> 24) & 0xFF;
    hdr[28] = byte_rate & 0xFF; hdr[29] = (byte_rate >> 8) & 0xFF;
    hdr[30] = (byte_rate >> 16) & 0xFF; hdr[31] = (byte_rate >> 24) & 0xFF;
    hdr[32] = block_align & 0xFF; hdr[33] = (block_align >> 8) & 0xFF;
    hdr[34] = bits & 0xFF; hdr[35] = (bits >> 8) & 0xFF;
    memcpy(hdr + 36, "data", 4);
    hdr[40] = data_size & 0xFF; hdr[41] = (data_size >> 8) & 0xFF;
    hdr[42] = (data_size >> 16) & 0xFF; hdr[43] = (data_size >> 24) & 0xFF;

    fwrite(hdr, 1, 44, f);
}

int main(int argc, char **argv) {
    uint32_t target_rate = 0;
    faar_quality_t quality = FAAR_QUALITY_GOOD;
    uint16_t channels = 2;
    pcm_format_t fmt = FMT_F32LE;
    faar_dither_t dither = FAAR_DITHER_OFF;
    const char *input_path = NULL;
    const char *output_path = NULL;

    static struct option long_options[] = {
        {"rate", required_argument, 0, 'r'},
        {"quality", required_argument, 0, 'q'},
        {"channels", required_argument, 0, 'c'},
        {"format", required_argument, 0, 'f'},
        {"dither", required_argument, 0, 'd'},
        {"output", required_argument, 0, 'o'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "r:q:c:f:d:o:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'r': target_rate = (uint32_t)atoi(optarg); break;
            case 'q':
                if (strcmp(optarg, "fast") == 0) quality = FAAR_QUALITY_FAST;
                else if (strcmp(optarg, "high") == 0) quality = FAAR_QUALITY_HIGH;
                else quality = FAAR_QUALITY_GOOD;
                break;
            case 'c': channels = (uint16_t)atoi(optarg); break;
            case 'f':
                if (strcmp(optarg, "s16le") == 0) fmt = FMT_S16LE;
                else if (strcmp(optarg, "s24le") == 0) fmt = FMT_S24LE;
                else fmt = FMT_F32LE;
                break;
            case 'd':
                if (strcmp(optarg, "tpdf") == 0) dither = FAAR_DITHER_TPDF;
                else if (strcmp(optarg, "shaped") == 0) dither = FAAR_DITHER_SHAPED;
                else dither = FAAR_DITHER_OFF;
                break;
            case 'o': output_path = optarg; break;
            case 'h': show_help(); return 0;
            default: show_help(); return 1;
        }
    }

    if (optind < argc && !input_path) input_path = argv[optind++];
    if (optind < argc && !output_path) output_path = argv[optind++];

    if (!input_path || target_rate == 0) {
        fprintf(stderr, "Error: Input path and target rate (-r <Hz>) are required.\n");
        show_help();
        return 1;
    }

    FILE *fin = (strcmp(input_path, "-") == 0) ? stdin : fopen(input_path, "rb");
    if (!fin) {
        fprintf(stderr, "Error: Couldn't open input file %s\n", input_path);
        return 1;
    }

#ifdef _WIN32
    if (fin == stdin) _setmode(_fileno(stdin), _O_BINARY);
#endif

    uint32_t in_rate = 44100;
    uint16_t in_channels = channels;
    uint16_t in_bits = 32;
    pcm_format_t in_fmt = fmt;
    uint32_t in_data_size = 0;
    bool is_wav_input = false;

    if (fin != stdin) {
        if (parse_wav_header(fin, &in_rate, &in_channels, &in_bits, &in_fmt, &in_data_size)) {
            is_wav_input = true;
            channels = in_channels;
        } else {
            fseek(fin, 0, SEEK_SET);
        }
    }

    FILE *fout = NULL;
    if (!output_path || strcmp(output_path, "-") == 0) {
        fout = stdout;
#ifdef _WIN32
        _setmode(_fileno(stdout), _O_BINARY);
#endif
    } else {
        fout = fopen(output_path, "wb");
        if (!fout) {
            fprintf(stderr, "Error: Couldn't open output file %s\n", output_path);
            if (fin != stdin) fclose(fin);
            return 1;
        }
    }

    bool write_wav_out = (fout != stdout && (is_wav_input || (output_path && strstr(output_path, ".wav"))));

    faar_handle_t r = faar_create(in_rate, target_rate, channels, quality);
    if (!r) {
        fprintf(stderr, "Error: Couldn't create resampler instance for %u Hz -> %u Hz\n", in_rate, target_rate);
        if (fin != stdin) fclose(fin);
        if (fout != stdout) fclose(fout);
        return 1;
    }

    if (dither != FAAR_DITHER_OFF && (fmt == FMT_S16LE || fmt == FMT_S24LE)) {
        uint8_t target_bits = (fmt == FMT_S16LE) ? 16 : 24;
        faar_set_dither(r, dither, target_bits);
    }

    if (write_wav_out) {
        write_wav_header(fout, target_rate, channels, fmt, 0); /* Placeholder size */
    }

    size_t chunk_frames = 2048;
    size_t in_bytes_per_sample = (in_fmt == FMT_F32LE ? 4 : (in_fmt == FMT_S24LE ? 3 : 2));
    size_t in_bytes_per_frame = channels * in_bytes_per_sample;
    uint8_t *raw_in_buf = (uint8_t *)malloc(chunk_frames * in_bytes_per_frame);
    float *float_in_buf = (float *)malloc(chunk_frames * channels * sizeof(float));

    size_t out_cap = chunk_frames * 4;
    float *float_out_buf = (float *)malloc(out_cap * channels * sizeof(float));
    size_t out_bytes_per_sample = (fmt == FMT_F32LE ? 4 : (fmt == FMT_S24LE ? 3 : 2));
    uint8_t *raw_out_buf = (uint8_t *)malloc(out_cap * channels * out_bytes_per_sample);

    uint32_t total_out_data_bytes = 0;

    size_t read_bytes = 0;
    while ((read_bytes = fread(raw_in_buf, 1, chunk_frames * in_bytes_per_frame, fin)) > 0) {
        size_t frames_read = read_bytes / in_bytes_per_frame;

        /* Convert raw input PCM to float */
        for (size_t i = 0; i < frames_read * channels; i++) {
            if (in_fmt == FMT_F32LE) {
                float_in_buf[i] = ((float *)raw_in_buf)[i];
            } else if (in_fmt == FMT_S16LE) {
                int16_t v = ((int16_t *)raw_in_buf)[i];
                float_in_buf[i] = (float)v / 32768.0f;
            } else if (in_fmt == FMT_S24LE) {
                int32_t v = (raw_in_buf[i * 3 + 0]) | (raw_in_buf[i * 3 + 1] << 8) | ((int8_t)raw_in_buf[i * 3 + 2] << 16);
                float_in_buf[i] = (float)v / 8388608.0f;
            }
        }

        size_t produced = faar_process(r, float_in_buf, frames_read, float_out_buf, out_cap);

        /* Convert float output PCM to requested format */
        for (size_t i = 0; i < produced * channels; i++) {
            float val = float_out_buf[i];
            if (val > 1.0f) val = 1.0f;
            if (val < -1.0f) val = -1.0f;

            if (fmt == FMT_F32LE) {
                ((float *)raw_out_buf)[i] = val;
            } else if (fmt == FMT_S16LE) {
                int32_t v = (int32_t)(val * 32768.0f + (val >= 0.0f ? 0.5f : -0.5f));
                if (v > 32767) v = 32767;
                if (v < -32768) v = -32768;
                ((int16_t *)raw_out_buf)[i] = (int16_t)v;
            } else if (fmt == FMT_S24LE) {
                int32_t v = (int32_t)(val * 8388608.0f + (val >= 0.0f ? 0.5f : -0.5f));
                if (v > 8388607) v = 8388607;
                if (v < -8388608) v = -8388608;
                raw_out_buf[i * 3 + 0] = v & 0xFF;
                raw_out_buf[i * 3 + 1] = (v >> 8) & 0xFF;
                raw_out_buf[i * 3 + 2] = (v >> 16) & 0xFF;
            }
        }

        size_t bytes_to_write = produced * channels * out_bytes_per_sample;
        fwrite(raw_out_buf, 1, bytes_to_write, fout);
        total_out_data_bytes += bytes_to_write;
    }

    size_t flushed = faar_flush(r, float_out_buf, out_cap);
    if (flushed > 0) {
        for (size_t i = 0; i < flushed * channels; i++) {
            float val = float_out_buf[i];
            if (val > 1.0f) val = 1.0f;
            if (val < -1.0f) val = -1.0f;

            if (fmt == FMT_F32LE) {
                ((float *)raw_out_buf)[i] = val;
            } else if (fmt == FMT_S16LE) {
                int32_t v = (int32_t)(val * 32767.0f + (val >= 0.0f ? 0.5f : -0.5f));
                if (v > 32767) v = 32767;
                if (v < -32768) v = -32768;
                ((int16_t *)raw_out_buf)[i] = (int16_t)v;
            } else if (fmt == FMT_S24LE) {
                int32_t v = (int32_t)(val * 8388607.0f + (val >= 0.0f ? 0.5f : -0.5f));
                if (v > 8388607) v = 8388607;
                if (v < -8388608) v = -8388608;
                raw_out_buf[i * 3 + 0] = v & 0xFF;
                raw_out_buf[i * 3 + 1] = (v >> 8) & 0xFF;
                raw_out_buf[i * 3 + 2] = (v >> 16) & 0xFF;
            }
        }

        size_t bytes_to_write = flushed * channels * out_bytes_per_sample;
        fwrite(raw_out_buf, 1, bytes_to_write, fout);
        total_out_data_bytes += bytes_to_write;
    }

    if (write_wav_out && fout != stdout) {
        fseek(fout, 0, SEEK_SET);
        write_wav_header(fout, target_rate, channels, fmt, total_out_data_bytes);
    }

    faar_destroy(r);
    free(raw_in_buf);
    free(float_in_buf);
    free(float_out_buf);
    free(raw_out_buf);

    if (fin != stdin) fclose(fin);
    if (fout != stdout) fclose(fout);

    return 0;
}
