/*
 * FAAD CLI Executable - Mimics faad2 interface with ADTS and MP4 support
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

extern bool mp4_read_track(FILE *f, MP4Track *track);
extern void mp4_free_track(MP4Track *track);

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
    printf("Usage: %s [options] infile.aac|infile.m4a\n", prog);
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

    MP4Track track;
    bool is_mp4 = mp4_read_track(fin, &track);

    fseek(fin, 0, SEEK_END);
    long file_len = ftell(fin);
    fseek(fin, 0, SEEK_SET);

    uint8_t *inbuf = (uint8_t *)malloc(file_len);
    if (!inbuf) {
        fclose(fin);
        if (is_mp4) mp4_free_track(&track);
        return 1;
    }

    if (fread(inbuf, 1, file_len, fin) != (size_t)file_len) {
        fprintf(stderr, "Error reading input file\n");
        free(inbuf);
        fclose(fin);
        if (is_mp4) mp4_free_track(&track);
        return 1;
    }
    fclose(fin);

    faad_params params;
    faad_params_init(&params, sizeof(params));
    params.stream_format = is_mp4 ? FAAD_STREAM_RAW : FAAD_STREAM_ADTS;
    params.output_format = FAAD_OUTPUT_16BIT;

    faad_decoder *dec = NULL;
    faad_status st = faad_decoder_open(&params, is_mp4 ? track.asc_buf : NULL, is_mp4 ? track.asc_len : 0, &dec);
    if (st != FAAD_OK) {
        fprintf(stderr, "Failed to open FAAD decoder: %s\n", faad_strerror(st));
        free(inbuf);
        if (is_mp4) mp4_free_track(&track);
        return 1;
    }

    FILE *fout = NULL;
    if (!info_only) {
        fout = fopen(outfile, "wb");
        if (!fout) {
            fprintf(stderr, "Error opening output file %s\n", outfile);
            faad_decoder_close(&dec);
            free(inbuf);
            if (is_mp4) mp4_free_track(&track);
            return 1;
        }
        write_wav_header(fout, 44100, 2, 0);
    }

    uint8_t outbuf[65536];
    uint32_t total_pcm_bytes = 0;
    uint32_t sample_rate = 44100;
    uint32_t num_channels = 2;
    uint32_t frames_decoded = 0;

    if (is_mp4) {
        for (uint32_t s = 0; s < track.num_samples; s++) {
            uint32_t offset = track.samples[s].offset;
            uint32_t size = track.samples[s].size;
            if (offset + size > (uint32_t)file_len) continue;

            uint32_t bytes_consumed = 0;
            uint32_t bytes_written = 0;

            st = faad_decoder_decode(dec, inbuf + offset, size,
                                     &bytes_consumed, outbuf, sizeof(outbuf), &bytes_written);

            if (st == FAAD_OK) {
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
            }
        }
    } else {
        uint32_t offset = 0;
        while (offset < (uint32_t)file_len) {
            uint32_t bytes_consumed = 0;
            uint32_t bytes_written = 0;

            st = faad_decoder_decode(dec, inbuf + offset, file_len - offset,
                                     &bytes_consumed, outbuf, sizeof(outbuf), &bytes_written);

            if (st != FAAD_OK) {
                if (st == FAAD_ERR_NEED_MORE_DATA || bytes_consumed == 0) {
                    break;
                }
                offset += 1;
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
    }

    if (info_only) {
        printf("AAC file info (%s):\n", is_mp4 ? "MP4/M4A Container" : "ADTS Bitstream");
        printf("Sample rate   : %u Hz\n", sample_rate);
        printf("Channels      : %u\n", num_channels);
        printf("Total frames  : %u\n", frames_decoded);
        if (is_mp4 && track.delay > 0) {
            printf("Gapless delay : %u samples\n", track.delay);
            printf("Gapless padding: %u samples\n", track.padding);
        }
    } else if (fout) {
        write_wav_header(fout, sample_rate, (uint16_t)num_channels, total_pcm_bytes);
        fclose(fout);
        printf("Decoded %u frames (%u bytes) to %s\n", frames_decoded, total_pcm_bytes, outfile);
    }

    faad_decoder_close(&dec);
    free(inbuf);
    if (is_mp4) mp4_free_track(&track);
    return 0;
}
