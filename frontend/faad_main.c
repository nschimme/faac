/*
 * FAAD CLI Executable - Mimics faad2 interface
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "faad.h"

#define BUFFER_SIZE (1024 * 1024)

static void write_wav_header(FILE *f, uint32_t sample_rate, uint16_t num_channels, uint32_t total_pcm_bytes)
{
    fseek(f, 0, SEEK_SET);
    uint32_t file_size = 36 + total_pcm_bytes;
    uint32_t byte_rate = sample_rate * num_channels * 2;
    uint16_t block_align = num_channels * 2;
    uint16_t bits_per_sample = 16;

    fwrite("RIFF", 1, 4, f);
    fwrite(&file_size, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);

    uint32_t fmt_chunk_size = 16;
    uint16_t audio_format = 1; /* PCM */
    fwrite(&fmt_chunk_size, 4, 1, f);
    fwrite(&audio_format, 2, 1, f);
    fwrite(&num_channels, 2, 1, f);
    fwrite(&sample_rate, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    fwrite(&block_align, 2, 1, f);
    fwrite(&bits_per_sample, 2, 1, f);

    fwrite("data", 1, 4, f);
    fwrite(&total_pcm_bytes, 4, 1, f);
}

static void print_usage(const char *prog)
{
    printf("FAAD - Freeware Advanced Audio Decoder\n");
    printf("Usage: %s [options] infile.aac\n", prog);
    printf("Options:\n");
    printf("  -o <filename>  Set output WAV file name\n");
    printf("  -i             Display AAC file and bitstream information\n");
    printf("  -h             Show help summary\n");
}

int main(int argc, char **argv)
{
    const char *infile = NULL;
    const char *outfile = "output.wav";
    bool info_only = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            outfile = argv[++i];
        } else if (strcmp(argv[i], "-i") == 0) {
            info_only = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            infile = argv[i];
        }
    }

    if (!infile) {
        print_usage(argv[0]);
        return 1;
    }

    FILE *fin = fopen(infile, "rb");
    if (!fin) {
        fprintf(stderr, "Error opening input file %s\n", infile);
        return 1;
    }

    fseek(fin, 0, SEEK_END);
    long file_len = ftell(fin);
    fseek(fin, 0, SEEK_SET);

    uint8_t *inbuf = (uint8_t *)malloc(file_len);
    if (!inbuf) {
        fclose(fin);
        return 1;
    }

    if (fread(inbuf, 1, file_len, fin) != (size_t)file_len) {
        fprintf(stderr, "Error reading input file\n");
        free(inbuf);
        fclose(fin);
        return 1;
    }
    fclose(fin);

    faad_params params;
    faad_params_init(&params, sizeof(params));
    params.stream_format = FAAD_STREAM_ADTS;
    params.output_format = FAAD_OUTPUT_16BIT;

    faad_decoder *dec = NULL;
    faad_status st = faad_decoder_open(&params, NULL, 0, &dec);
    if (st != FAAD_OK) {
        fprintf(stderr, "Failed to open FAAD decoder: %s\n", faad_strerror(st));
        free(inbuf);
        return 1;
    }

    FILE *fout = NULL;
    if (!info_only) {
        fout = fopen(outfile, "wb");
        if (!fout) {
            fprintf(stderr, "Error opening output file %s\n", outfile);
            faad_decoder_close(&dec);
            free(inbuf);
            return 1;
        }
        /* Write dummy header */
        write_wav_header(fout, 44100, 2, 0);
    }

    uint8_t outbuf[65536];
    uint32_t offset = 0;
    uint32_t total_pcm_bytes = 0;
    uint32_t sample_rate = 44100;
    uint32_t num_channels = 2;
    uint32_t frames_decoded = 0;

    while (offset < (uint32_t)file_len) {
        uint32_t bytes_consumed = 0;
        uint32_t bytes_written = 0;

        st = faad_decoder_decode(dec, inbuf + offset, file_len - offset,
                                 &bytes_consumed, outbuf, sizeof(outbuf), &bytes_written);

        if (st != FAAD_OK) {
            if (st == FAAD_ERR_NEED_MORE_DATA || bytes_consumed == 0) {
                break;
            }
            offset += 1; /* Skip corrupted byte */
            continue;
        }

        faad_decoder_info info;
        info.struct_size = sizeof(info);
        if (faad_decoder_get_info(dec, &info) == FAAD_OK) {
            sample_rate = info.sample_rate;
            num_channels = info.num_channels;
        }

        if (fout && bytes_written > 0) {
            fwrite(outbuf, 1, bytes_written, fout);
            total_pcm_bytes += bytes_written;
        }

        frames_decoded++;
        offset += bytes_consumed;
    }

    if (info_only) {
        printf("AAC file info:\n");
        printf("Sample rate : %u Hz\n", sample_rate);
        printf("Channels    : %u\n", num_channels);
        printf("Total frames: %u\n", frames_decoded);
    } else if (fout) {
        write_wav_header(fout, sample_rate, (uint16_t)num_channels, total_pcm_bytes);
        fclose(fout);
        printf("Decoded %u frames (%u bytes) to %s\n", frames_decoded, total_pcm_bytes, outfile);
    }

    faad_decoder_close(&dec);
    free(inbuf);
    return 0;
}
