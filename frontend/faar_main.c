/*
 * faar - CPU Audio Resampler CLI Tool
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
#include "input.h"
#include "cli_io.h"
#include "help.h"

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

typedef enum {
    FMT_F32LE = 0,
    FMT_S16LE = 1,
    FMT_S24LE = 2
} pcm_format_t;

static const help_t core_help[] = {
    {"-r, --rate <Hz>", "Target sample rate (Required, e.g., 48000, 96000, 192000, 384000)", NULL},
    {"-R, --in-rate <Hz>", "Raw PCM input sample rate in Hz [default: 44100]", NULL},
    {"--quality <preset>", "DSP filter quality (fast, good, high) [default: good]", NULL},
    {"--boundary <mode>", "Endpoint extension (hold, zero) [default: hold]", NULL},
    {"-c, --channels <num>", "Audio channel count [default: 2]", NULL},
    {"-f, --format <fmt>", "Raw PCM sample format (f32le, s16le, s24le) [default: f32le]", NULL},
    {"-d, --dither <type>", "Dithering type for integer format output (none, tpdf, shaped) [default: none]", NULL},
    {"--decimate", "Force multi-stage decimation cascade for large downsampling ratios", NULL},
    {"--ignorelength", "Ignore the WAV data length in the header and read to EOF", NULL},
    {"-v <level>", "Set verbosity level (-v0 is quiet mode) [default: 1]", NULL},
    {"-o, --output <path>", "Explicit output path (or use - for stdout)", NULL},
    {"-h, --help", "Display usage guide", NULL},
    {NULL, NULL, NULL}
};

static const help_group_t help_groups[] = {
    {1, "Core options", "--help", core_help, 1},
    {0, NULL, NULL, NULL, 0}
};

static void print_faar_help(const char *prog_name, int mode) {
    show_help(prog_name, mode, "3.0.0", help_groups);
    printf(
        "\nExamples:\n"
        "  faar -r 48000 input_96k.wav output_48k.wav\n"
        "  faar -r 48000 --quality high -d tpdf -f s16le input.wav output.wav\n"
        "  cat input.raw | faar -r 48000 -c 2 -f f32le - - | aplay -f S16_LE -r 48000\n"
    );
}

static int write_output_frames(FILE *fout, const float *samples, uint8_t *raw_out_buf,
                               size_t frames, uint16_t channels, pcm_format_t fmt,
                               size_t bytes_per_sample, uint64_t *total_bytes) {
    size_t sample_count = frames * channels;

    /* Convert float output PCM ([-32768, +32768]) to the requested format. */
    for (size_t i = 0; i < sample_count; i++) {
        float val = samples[i];

        if (fmt == FMT_F32LE) {
            float norm_val = val / 32768.0f;
            if (norm_val > 1.0f) norm_val = 1.0f;
            if (norm_val < -1.0f) norm_val = -1.0f;
            ((float *)raw_out_buf)[i] = norm_val;
        } else if (fmt == FMT_S16LE) {
            int32_t v = (int32_t)(val + (val >= 0.0f ? 0.5f : -0.5f));
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            ((int16_t *)raw_out_buf)[i] = (int16_t)v;
        } else if (fmt == FMT_S24LE) {
            int32_t v = (int32_t)(val * 256.0f + (val >= 0.0f ? 0.5f : -0.5f));
            if (v > 8388607) v = 8388607;
            if (v < -8388608) v = -8388608;
            raw_out_buf[i * 3 + 0] = v & 0xFF;
            raw_out_buf[i * 3 + 1] = (v >> 8) & 0xFF;
            raw_out_buf[i * 3 + 2] = (v >> 16) & 0xFF;
        }
    }

    size_t bytes_to_write = frames * channels * bytes_per_sample;
    if (bytes_to_write && fwrite(raw_out_buf, 1, bytes_to_write, fout) != bytes_to_write) {
        return -1;
    }
    *total_bytes += bytes_to_write;
    return 0;
}


static int write_wav_header(FILE *f, uint32_t rate, uint16_t channels, pcm_format_t fmt, uint64_t data_size) {
    uint16_t bits = (fmt == FMT_F32LE) ? 32 : (fmt == FMT_S24LE ? 24 : 16);
    uint16_t audio_fmt = (fmt == FMT_F32LE) ? 3 : 1;
    uint32_t block_align = channels * (bits / 8);
    uint32_t byte_rate = rate * block_align;
    bool is_rf64 = (data_size >= 0xFFFFFFFFULL || (data_size + 36) >= 0xFFFFFFFFULL);

    if (is_rf64) {
        uint8_t hdr[80] = {0};
        uint64_t riff_size = data_size + 72;

        memcpy(hdr, "RF64", 4);
        memset(hdr + 4, 0xFF, 4);
        memcpy(hdr + 8, "WAVE", 4);

        memcpy(hdr + 12, "ds64", 4);
        hdr[16] = 28; /* ds64 chunk size */
        hdr[20] = riff_size & 0xFF; hdr[21] = (riff_size >> 8) & 0xFF;
        hdr[22] = (riff_size >> 16) & 0xFF; hdr[23] = (riff_size >> 24) & 0xFF;
        hdr[24] = (riff_size >> 32) & 0xFF; hdr[25] = (riff_size >> 40) & 0xFF;
        hdr[26] = (riff_size >> 48) & 0xFF; hdr[27] = (riff_size >> 56) & 0xFF;

        hdr[28] = data_size & 0xFF; hdr[29] = (data_size >> 8) & 0xFF;
        hdr[30] = (data_size >> 16) & 0xFF; hdr[31] = (data_size >> 24) & 0xFF;
        hdr[32] = (data_size >> 32) & 0xFF; hdr[33] = (data_size >> 40) & 0xFF;
        hdr[34] = (data_size >> 48) & 0xFF; hdr[35] = (data_size >> 56) & 0xFF;

        uint64_t sample_count = data_size / block_align;
        hdr[36] = sample_count & 0xFF; hdr[37] = (sample_count >> 8) & 0xFF;
        hdr[38] = (sample_count >> 16) & 0xFF; hdr[39] = (sample_count >> 24) & 0xFF;
        hdr[40] = (sample_count >> 32) & 0xFF; hdr[41] = (sample_count >> 40) & 0xFF;
        hdr[42] = (sample_count >> 48) & 0xFF; hdr[43] = (sample_count >> 56) & 0xFF;

        memcpy(hdr + 48, "fmt ", 4);
        hdr[52] = 16;
        hdr[56] = audio_fmt & 0xFF; hdr[57] = (audio_fmt >> 8) & 0xFF;
        hdr[58] = channels & 0xFF; hdr[59] = (channels >> 8) & 0xFF;
        hdr[60] = rate & 0xFF; hdr[61] = (rate >> 8) & 0xFF;
        hdr[62] = (rate >> 16) & 0xFF; hdr[63] = (rate >> 24) & 0xFF;
        hdr[64] = byte_rate & 0xFF; hdr[65] = (byte_rate >> 8) & 0xFF;
        hdr[66] = (byte_rate >> 16) & 0xFF; hdr[67] = (byte_rate >> 24) & 0xFF;
        hdr[68] = block_align & 0xFF; hdr[69] = (block_align >> 8) & 0xFF;
        hdr[70] = bits & 0xFF; hdr[71] = (bits >> 8) & 0xFF;

        memcpy(hdr + 72, "data", 4);
        memset(hdr + 76, 0xFF, 4);

        return fwrite(hdr, 1, sizeof(hdr), f) == sizeof(hdr) ? 0 : -1;
    } else {
        uint8_t hdr[44] = {0};
        uint32_t riff_size = (uint32_t)(data_size + 36);

        memcpy(hdr, "RIFF", 4);
        hdr[4] = riff_size & 0xFF; hdr[5] = (riff_size >> 8) & 0xFF;
        hdr[6] = (riff_size >> 16) & 0xFF; hdr[7] = (riff_size >> 24) & 0xFF;
        memcpy(hdr + 8, "WAVEfmt ", 8);
        hdr[16] = 16;
        hdr[20] = audio_fmt & 0xFF; hdr[21] = (audio_fmt >> 8) & 0xFF;
        hdr[22] = channels & 0xFF; hdr[23] = (channels >> 8) & 0xFF;
        hdr[24] = rate & 0xFF; hdr[25] = (rate >> 8) & 0xFF;
        hdr[26] = (rate >> 16) & 0xFF; hdr[27] = (rate >> 24) & 0xFF;
        hdr[28] = byte_rate & 0xFF; hdr[29] = (byte_rate >> 8) & 0xFF;
        hdr[30] = (byte_rate >> 16) & 0xFF; hdr[31] = (byte_rate >> 24) & 0xFF;
        hdr[32] = block_align & 0xFF; hdr[33] = (block_align >> 8) & 0xFF;
        hdr[34] = bits & 0xFF; hdr[35] = (bits >> 8) & 0xFF;
        memcpy(hdr + 36, "data", 4);
        hdr[40] = (uint32_t)data_size & 0xFF; hdr[41] = ((uint32_t)data_size >> 8) & 0xFF;
        hdr[42] = ((uint32_t)data_size >> 16) & 0xFF; hdr[43] = ((uint32_t)data_size >> 24) & 0xFF;

        return fwrite(hdr, 1, sizeof(hdr), f) == sizeof(hdr) ? 0 : -1;
    }
}

int main(int argc, char **argv) {
    uint32_t target_rate = 0;
    uint32_t raw_in_rate = 44100;
    faar_quality_t quality = FAAR_QUALITY_GOOD;
    uint16_t channels = 2;
    pcm_format_t fmt = FMT_F32LE;
    faar_dither_t dither = FAAR_DITHER_OFF;
    faar_boundary_t boundary = FAAR_BOUNDARY_HOLD;
    const char *input_path = NULL;
    const char *output_path = NULL;

    uint8_t verbose = 1;
    bool ignore_wav_length = false;
    bool allow_extreme_rates = false;

    uint32_t decimate_factor = 0;

    static struct option long_options[] = {
        {"rate", required_argument, 0, 'r'},
        {"in-rate", required_argument, 0, 'R'},
        {"quality", required_argument, 0, 1000},
        {"boundary", required_argument, 0, 1002},
        {"channels", required_argument, 0, 'c'},
        {"format", required_argument, 0, 'f'},
        {"dither", required_argument, 0, 'd'},
        {"decimate", optional_argument, 0, 'D'},
        {"allow-extreme-rates", no_argument, 0, 1001},
        {"ignorelength", no_argument, 0, 'I'},
        {"output", required_argument, 0, 'o'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "r:R:c:f:d:v:I::D::o:hH", long_options, NULL)) != -1) {
        switch (opt) {
            case 'r': target_rate = (uint32_t)atoi(optarg); break;
            case 'R': raw_in_rate = (uint32_t)atoi(optarg); break;
            case 1000:
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
            case 1001: allow_extreme_rates = true; break;
            case 1002:
                if (strcmp(optarg, "hold") == 0) boundary = FAAR_BOUNDARY_HOLD;
                else if (strcmp(optarg, "zero") == 0) boundary = FAAR_BOUNDARY_ZERO;
                else { fprintf(stderr, "Invalid boundary mode: %s\n", optarg); return 1; }
                break;
            case 'v': verbose = (uint8_t)atoi(optarg); break;
            case 'I': ignore_wav_length = true; break;
            case 'D':
                decimate_factor = (optarg && atoi(optarg) > 0) ? (uint32_t)atoi(optarg) : 2;
                break;
            case 'o': output_path = optarg; break;
            case 'h': print_faar_help(argv[0], 'h'); return 0;
            case 'H': print_faar_help(argv[0], 'H'); return 0;
            default: print_faar_help(argv[0], 'h'); return 1;
        }
    }

    if (optind < argc && !input_path) input_path = argv[optind++];
    if (optind < argc && !output_path) output_path = argv[optind++];

    if (!input_path || target_rate == 0) {
        print_faar_help(argv[0], 'h');
        return 1;
    }

    uint32_t max_rate = allow_extreme_rates ? 384000 : 192000;
    if (target_rate < 7350 || target_rate > max_rate) {
        if (target_rate > 192000 && !allow_extreme_rates) {
            fprintf(stderr, "Error: Target sample rate %u Hz exceeds standard audio rate limit (192000 Hz).\n"
                            "       Use --allow-extreme-rates to permit rates up to 384000 Hz.\n", target_rate);
        } else {
            fprintf(stderr, "Error: Target sample rate %u Hz is outside valid range (7350 Hz to %u Hz).\n", target_rate, max_rate);
        }
        return 1;
    }

    pcmfile_t *infile = wav_open_read(input_path, false);
    bool is_wav_input = (infile != NULL);

    uint32_t in_rate = raw_in_rate;
    if (is_wav_input) {
        if (ignore_wav_length) infile->bounded_data = false;
        in_rate = infile->samplerate;
        channels = infile->channels;
    } else {
        infile = wav_open_read(input_path, true);
        if (!infile) {
            fprintf(stderr, "Error: Couldn't open input file %s\n", input_path);
            return 1;
        }
        infile->channels = channels;
        infile->samplebytes = (fmt == FMT_F32LE) ? 4 : (fmt == FMT_S24LE ? 3 : 2);
        infile->samplerate = raw_in_rate;
        infile->isfloat = (fmt == FMT_F32LE);
        /* All raw formats exposed by this CLI are little-endian. */
        infile->bigendian = false;
    }

    FILE *fout = NULL;
    if (!output_path || strcmp(output_path, "-") == 0) {
        fout = stdout;
#ifdef _WIN32
        _setmode(_fileno(stdout), _O_BINARY);
#endif
    } else {
        fout = cli_fopen(output_path, "wb");
        if (!fout) {
            fprintf(stderr, "Error: Couldn't open output file %s\n", output_path);
            if (infile) wav_close(infile);
            return 1;
        }
    }

    bool write_wav_out = (fout != stdout && (is_wav_input || (output_path && strstr(output_path, ".wav"))));

    faar_handle_t r = faar_create_ext(in_rate, target_rate, channels, quality, decimate_factor);
    if (!r) {
        fprintf(stderr, "Error: Couldn't create resampler instance for %u Hz -> %u Hz\n", in_rate, target_rate);
        if (infile) wav_close(infile);
        if (fout != stdout) fclose(fout);
        return 1;
    }

    faar_set_boundary(r, boundary);
    if (dither != FAAR_DITHER_OFF && (fmt == FMT_S16LE || fmt == FMT_S24LE)) {
        uint8_t target_bits = (fmt == FMT_S16LE) ? 16 : 24;
        faar_set_dither(r, dither, target_bits);
    }

    if (write_wav_out) {
        /* Reserve 44-byte header initially at file start */
        uint8_t header_placeholder[44] = {0};
        if (fwrite(header_placeholder, 1, 44, fout) != sizeof(header_placeholder)) {
            fprintf(stderr, "Error: Couldn't write WAV header to output\n");
            faar_destroy(r);
            if (infile) wav_close(infile);
            if (fout != stdout) fclose(fout);
            return 1;
        }
    }

    size_t chunk_frames = 2048;
    float *float_in_buf = (float *)malloc(chunk_frames * channels * sizeof(float));

    size_t out_cap = chunk_frames * 4;
    float *float_out_buf = (float *)malloc(out_cap * channels * sizeof(float));
    size_t out_bytes_per_sample = (fmt == FMT_F32LE ? 4 : (fmt == FMT_S24LE ? 3 : 2));
    uint8_t *raw_out_buf = (uint8_t *)malloc(out_cap * channels * out_bytes_per_sample);

    uint64_t total_out_data_bytes = 0;
    if (verbose > 0) {
        fprintf(stderr, "Resampling input: %u Hz -> %u Hz (%u channels)\n", in_rate, target_rate, channels);
    }

    size_t samples_read = 0;
    while ((samples_read = wav_read_float32(infile, float_in_buf, chunk_frames * channels, NULL)) > 0) {
        size_t frames_read = samples_read / channels;
        size_t produced = faar_process(r, float_in_buf, frames_read, float_out_buf, out_cap);
        for (;;) {
            if (write_output_frames(fout, float_out_buf, raw_out_buf, produced, channels,
                                    fmt, out_bytes_per_sample, &total_out_data_bytes) != 0) {
                fprintf(stderr, "Error: Couldn't write converted audio to output\n");
                faar_destroy(r);
                free(float_in_buf);
                free(float_out_buf);
                free(raw_out_buf);
                if (infile) wav_close(infile);
                if (fout != stdout) fclose(fout);
                return 1;
            }
            produced = faar_process(r, NULL, 0, float_out_buf, out_cap);
            if (produced == 0) break;
        }
    }

    for (;;) {
        size_t flushed = faar_flush(r, float_out_buf, out_cap);
        if (flushed == 0) break;
        if (write_output_frames(fout, float_out_buf, raw_out_buf, flushed, channels,
                                fmt, out_bytes_per_sample, &total_out_data_bytes) != 0) {
            fprintf(stderr, "Error: Couldn't write flushed audio to output\n");
            faar_destroy(r);
            free(float_in_buf);
            free(float_out_buf);
            free(raw_out_buf);
            if (infile) wav_close(infile);
            if (fout != stdout) fclose(fout);
            return 1;
        }
    }

    if (write_wav_out && fout != stdout) {
        if (fseek(fout, 0, SEEK_SET) != 0) {
            fprintf(stderr, "Error: Couldn't seek to WAV header in output\n");
            faar_destroy(r);
            free(float_in_buf);
            free(float_out_buf);
            free(raw_out_buf);
            if (infile) wav_close(infile);
            fclose(fout);
            return 1;
        }
        if (write_wav_header(fout, target_rate, channels, fmt, total_out_data_bytes) != 0) {
            fprintf(stderr, "Error: Couldn't write WAV header to output\n");
            faar_destroy(r);
            free(float_in_buf);
            free(float_out_buf);
            free(raw_out_buf);
            if (infile) wav_close(infile);
            fclose(fout);
            return 1;
        }
    }

    faar_destroy(r);
    free(float_in_buf);
    free(float_out_buf);
    free(raw_out_buf);

    if (infile) wav_close(infile);
    if (fout != stdout) fclose(fout);

    return 0;
}
