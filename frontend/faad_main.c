/*
 * FAAD CLI Executable - Modernized Unix Audio Decoder Utility
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "faad.h"

typedef struct {
    uint64_t offset;
    uint32_t size;
} MP4Sample;

typedef struct {
    uint8_t *asc_buf;
    uint32_t asc_len;
    uint32_t delay;
    uint32_t padding;
    MP4Sample *samples;
    uint32_t num_samples;
    char major_brand[16];
    char encoder_tag[64];
} MP4Track;

extern bool mp4_read_track_buf(const uint8_t *buf, long file_size, MP4Track *track);
extern void mp4_free_track(MP4Track *track);

typedef struct {
    uint8_t *data;
    uint32_t size;
    uint32_t head;
    uint32_t tail;
    uint32_t fill;
} PCMFifo;

static void fifo_init(PCMFifo *f, uint32_t capacity)
{
    f->data = (uint8_t *)malloc(capacity > 0 ? capacity : 65536);
    f->size = capacity > 0 ? capacity : 65536;
    f->head = 0;
    f->tail = 0;
    f->fill = 0;
}

static void fifo_free(PCMFifo *f)
{
    if (f->data) free(f->data);
    f->data = NULL;
    f->size = 0;
    f->head = 0;
    f->tail = 0;
    f->fill = 0;
}

static void fifo_push(PCMFifo *f, const uint8_t *src, uint32_t len)
{
    if (len == 0) return;
    if (f->fill + len > f->size) {
        uint32_t new_size = f->size * 2;
        while (new_size < f->fill + len) new_size *= 2;
        uint8_t *new_data = (uint8_t *)malloc(new_size);
        if (f->fill > 0) {
            if (f->tail < f->head) {
                memcpy(new_data, f->data + f->tail, f->fill);
            } else {
                uint32_t first = f->size - f->tail;
                memcpy(new_data, f->data + f->tail, first);
                memcpy(new_data + first, f->data, f->head);
            }
        }
        free(f->data);
        f->data = new_data;
        f->size = new_size;
        f->tail = 0;
        f->head = f->fill;
    }
    uint32_t first = f->size - f->head;
    if (len <= first) {
        memcpy(f->data + f->head, src, len);
        f->head = (f->head + len) % f->size;
    } else {
        memcpy(f->data + f->head, src, first);
        memcpy(f->data, src + first, len - first);
        f->head = len - first;
    }
    f->fill += len;
}

static uint32_t fifo_pop(PCMFifo *f, uint8_t *dst, uint32_t len)
{
    if (len > f->fill) len = f->fill;
    if (len == 0) return 0;
    uint32_t first = f->size - f->tail;
    if (len <= first) {
        memcpy(dst, f->data + f->tail, len);
        f->tail = (f->tail + len) % f->size;
    } else {
        memcpy(dst, f->data + f->tail, first);
        memcpy(dst + first, f->data, len - first);
        f->tail = len - first;
    }
    f->fill -= len;
    return len;
}

static void fifo_truncate_tail(PCMFifo *f, uint32_t bytes_to_remove)
{
    if (bytes_to_remove >= f->fill) {
        f->head = 0;
        f->tail = 0;
        f->fill = 0;
        return;
    }
    if (f->head >= bytes_to_remove) {
        f->head -= bytes_to_remove;
    } else {
        f->head = f->size - (bytes_to_remove - f->head);
    }
    f->fill -= bytes_to_remove;
}

static void write_wav_header(FILE *f, uint32_t sample_rate, uint16_t num_channels, uint32_t total_pcm_bytes, uint16_t bits_per_sample, bool is_float)
{
    fseek(f, 0, SEEK_SET);
    uint32_t file_size = 36 + total_pcm_bytes;
    uint16_t bytes_per_sample = bits_per_sample / 8;
    uint32_t byte_rate = sample_rate * num_channels * bytes_per_sample;
    uint16_t block_align = num_channels * bytes_per_sample;

    fwrite("RIFF", 1, 4, f);
    fwrite(&file_size, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);

    uint32_t fmt_chunk_size = 16;
    uint16_t audio_format = is_float ? 3 : 1; /* 1 = PCM, 3 = IEEE Float */
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
    printf("Usage: %s [options] <infile.aac|infile.m4a>\n\n", prog);
    printf("I/O & Format Options:\n");
    printf("  -o, --output <file>    Set output filename (default: stdout if piped, or infile.wav)\n");
    printf("  -w, --stdout           Write output PCM to stdout\n");
    printf("  -f, --format <type>    Output container format: wav (default), raw\n");
    printf("  -b, --bits <depth>     Sample depth: 16 (default), 24, 32f (32-bit float)\n");
    printf("  -a, --adts <file>      Extract raw ADTS stream from MP4 without decoding\n\n");
    printf("Processing Options:\n");
    printf("  -d, --downmix [mode]   Downmix audio (mono/1 or stereo/2, default: mono)\n");
    printf("  -j, --jump <seconds>   Start decoding from specified timestamp\n");
    printf("      --no-gapless       Disable automatic gapless trim/padding handling\n\n");
    printf("Information & General:\n");
    printf("  -i, --info             Display bitstream & container metadata, then exit\n");
    printf("      --json             Output bitstream info in JSON format\n");
    printf("  -q, --quiet            Quiet mode (suppress decoding progress)\n");
    printf("  -h, --help             Display this help text\n");
}

int main(int argc, char **argv)
{
    const char *infile = NULL;
    const char *outfile = NULL;
    const char *adts_outfile = NULL;
    bool write_stdout = false;
    bool raw_format = false;
    uint32_t bit_depth = 16;
    bool is_float = false;
    bool downmix_stereo = false;
    bool gapless = true;
    bool info_only = false;
    bool json_info = false;
    bool quiet = false;
    double jump_seconds = 0.0;

    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) && i + 1 < argc) {
            outfile = argv[++i];
        } else if (strcmp(argv[i], "-w") == 0 || strcmp(argv[i], "--stdout") == 0) {
            write_stdout = true;
        } else if ((strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--format") == 0) && i + 1 < argc) {
            i++;
            if (strcmp(argv[i], "raw") == 0) raw_format = true;
        } else if ((strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--bits") == 0) && i + 1 < argc) {
            i++;
            if (strcmp(argv[i], "24") == 0) {
                bit_depth = 24;
            } else if (strcmp(argv[i], "32f") == 0 || strcmp(argv[i], "32") == 0) {
                bit_depth = 32;
                is_float = true;
            } else {
                bit_depth = 16;
            }
        } else if ((strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--adts") == 0) && i + 1 < argc) {
            adts_outfile = argv[++i];
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--downmix") == 0) {
            downmix_stereo = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                i++;
                /* Accept optional target downmix mode */
            }
        } else if ((strcmp(argv[i], "-j") == 0 || strcmp(argv[i], "--jump") == 0) && i + 1 < argc) {
            jump_seconds = atof(argv[++i]);
        } else if (strcmp(argv[i], "--no-gapless") == 0) {
            gapless = false;
        } else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--info") == 0) {
            info_only = true;
        } else if (strcmp(argv[i], "--json") == 0) {
            json_info = true;
            info_only = true;
        } else if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quiet") == 0) {
            quiet = true;
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

    uint8_t *inbuf = (uint8_t *)malloc(file_len > 0 ? file_len : 1);
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

    MP4Track track;
    memset(&track, 0, sizeof(track));
    bool is_mp4 = mp4_read_track_buf(inbuf, file_len, &track);

    /* Direct ADTS extraction from MP4 container without decoding */
    if (adts_outfile && is_mp4) {
        FILE *fadts = fopen(adts_outfile, "wb");
        if (!fadts) {
            fprintf(stderr, "Error opening ADTS output file %s\n", adts_outfile);
            free(inbuf);
            mp4_free_track(&track);
            return 1;
        }
        for (uint32_t s = 0; s < track.num_samples; s++) {
            uint64_t offset = track.samples[s].offset;
            uint32_t size = track.samples[s].size;
            if (offset > 0 && offset + size <= (uint64_t)file_len) {
                uint8_t adts_hdr[7] = { 0xFF, 0xF1, 0x50, 0x80, 0x00, 0x1F, 0xFC };
                uint32_t frame_len = size + 7;
                adts_hdr[3] = (uint8_t)(0x80 | ((frame_len >> 11) & 0x03));
                adts_hdr[4] = (uint8_t)((frame_len >> 3) & 0xFF);
                adts_hdr[5] = (uint8_t)(((frame_len & 0x07) << 5) | 0x1F);
                fwrite(adts_hdr, 1, 7, fadts);
                fwrite(inbuf + offset, 1, size, fadts);
            }
        }
        fclose(fadts);
        if (!quiet) printf("Extracted %u raw ADTS frames to %s\n", track.num_samples, adts_outfile);
        free(inbuf);
        mp4_free_track(&track);
        return 0;
    }

    faad_params params;
    faad_params_init(&params, sizeof(params));
    params.stream_format = is_mp4 ? FAAD_STREAM_RAW : FAAD_STREAM_ADTS;
    params.output_format = is_float ? FAAD_OUTPUT_FLOAT : FAAD_OUTPUT_16BIT;
    params.downmix_stereo = downmix_stereo;

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
        if (write_stdout) {
            fout = stdout;
            quiet = true;
        } else {
            if (!outfile) {
                char *out_path = (char *)malloc(strlen(infile) + 8);
                strcpy(out_path, infile);
                char *dot = strrchr(out_path, '.');
                if (dot) strcpy(dot, raw_format ? ".raw" : ".wav");
                else strcat(out_path, raw_format ? ".raw" : ".wav");
                outfile = out_path;
            }
            fout = fopen(outfile, "wb");
            if (!fout) {
                fprintf(stderr, "Error opening output file %s\n", outfile);
                faad_decoder_close(&dec);
                free(inbuf);
                if (is_mp4) mp4_free_track(&track);
                return 1;
            }
            if (!raw_format) {
                faad_decoder_info info;
                info.struct_size = sizeof(info);
                uint32_t init_sr = 44100;
                uint32_t init_ch = 2;
                if (faad_decoder_get_info(dec, &info) == FAAD_OK) {
                    if (info.sample_rate > 0) init_sr = info.sample_rate;
                    if (info.num_channels > 0) init_ch = info.num_channels;
                }
                write_wav_header(fout, init_sr, (uint16_t)init_ch, 0, bit_depth, is_float);
            }
        }
    }

    uint8_t outbuf[65536];
    uint8_t pcm24_buf[98304];
    uint32_t total_pcm_bytes = 0;
    uint32_t sample_rate = 44100;
    uint32_t num_channels = 2;
    enum faad_object_type obj_type = FAAD_OBJ_LOW;
    uint32_t frames_decoded = 0;

    uint32_t start_frame = 0;
    if (jump_seconds > 0.0) {
        faad_decoder_info info;
        info.struct_size = sizeof(info);
        uint32_t sr = 44100;
        uint32_t fl = 1024;
        if (faad_decoder_get_info(dec, &info) == FAAD_OK) {
            if (info.sample_rate > 0) sr = info.sample_rate;
            if (info.object_type == FAAD_OBJ_HE_AAC_V1 || info.object_type == FAAD_OBJ_HE_AAC_V2) {
                fl = 2048;
            }
        }
        start_frame = (uint32_t)((jump_seconds * (double)sr) / (double)fl);
    }

    uint32_t samples_to_skip = (is_mp4 && gapless) ? track.delay : 0;
    PCMFifo fifo;
    fifo_init(&fifo, 262144);

    if (is_mp4) {
        for (uint32_t s = start_frame; s < track.num_samples; s++) {
            uint64_t offset = track.samples[s].offset;
            uint32_t size = track.samples[s].size;
            if (offset == 0 || offset + size > (uint64_t)file_len) continue;

            uint32_t bytes_consumed = 0;
            uint32_t bytes_written = 0;

            st = faad_decoder_decode(dec, inbuf + offset, size,
                                     &bytes_consumed, outbuf, sizeof(outbuf), &bytes_written);

            if (st == FAAD_OK && bytes_written > 0) {
                faad_decoder_info info;
                info.struct_size = sizeof(info);
                if (faad_decoder_get_info(dec, &info) == FAAD_OK) {
                    sample_rate = info.sample_rate;
                    num_channels = info.num_channels;
                    obj_type = info.object_type;
                }

                uint32_t dec_bytes_per_sample = is_float ? 4 : 2;
                uint32_t dec_bytes_per_frame_sample = num_channels * dec_bytes_per_sample;
                uint32_t frame_samples = bytes_written / dec_bytes_per_frame_sample;

                uint8_t *write_ptr = outbuf;
                uint32_t samples_to_write = frame_samples;

                if (samples_to_skip > 0) {
                    if (samples_to_skip >= samples_to_write) {
                        samples_to_skip -= samples_to_write;
                        samples_to_write = 0;
                    } else {
                        write_ptr += samples_to_skip * dec_bytes_per_frame_sample;
                        samples_to_write -= samples_to_skip;
                        samples_to_skip = 0;
                    }
                }

                if (fout && samples_to_write > 0) {
                    if (bit_depth == 24 && !is_float) {
                        /* Convert int16_t PCM from decoder to 24-bit PCM */
                        const int16_t *src_pcm = (const int16_t *)write_ptr;
                        uint32_t total_items = samples_to_write * num_channels;
                        for (uint32_t k = 0; k < total_items; k++) {
                            int32_t val24 = ((int32_t)src_pcm[k]) << 8;
                            pcm24_buf[k * 3 + 0] = (uint8_t)(val24 & 0xFF);
                            pcm24_buf[k * 3 + 1] = (uint8_t)((val24 >> 8) & 0xFF);
                            pcm24_buf[k * 3 + 2] = (uint8_t)((val24 >> 16) & 0xFF);
                        }
                        fifo_push(&fifo, pcm24_buf, total_items * 3);
                    } else {
                        fifo_push(&fifo, write_ptr, samples_to_write * dec_bytes_per_frame_sample);
                    }

                    uint32_t padding_bytes = (gapless && track.padding > 0) ? (track.padding * num_channels * (bit_depth / 8)) : 0;
                    if (fifo.fill > padding_bytes) {
                        uint32_t can_pop = fifo.fill - padding_bytes;
                        uint8_t pop_buf[4096];
                        while (can_pop > 0) {
                            uint32_t chunk = can_pop < sizeof(pop_buf) ? can_pop : sizeof(pop_buf);
                            fifo_pop(&fifo, pop_buf, chunk);
                            fwrite(pop_buf, 1, chunk, fout);
                            total_pcm_bytes += chunk;
                            can_pop -= chunk;
                        }
                    }
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
                obj_type = info.object_type;
            }

            if (fout && bytes_written > 0) {
                uint32_t dec_bytes_per_sample = is_float ? 4 : 2;
                uint32_t dec_bytes_per_frame_sample = num_channels * dec_bytes_per_sample;
                uint32_t frame_samples = bytes_written / dec_bytes_per_frame_sample;

                if (bit_depth == 24 && !is_float) {
                    const int16_t *src_pcm = (const int16_t *)outbuf;
                    uint32_t total_items = frame_samples * num_channels;
                    for (uint32_t k = 0; k < total_items; k++) {
                        int32_t val24 = ((int32_t)src_pcm[k]) << 8;
                        pcm24_buf[k * 3 + 0] = (uint8_t)(val24 & 0xFF);
                        pcm24_buf[k * 3 + 1] = (uint8_t)((val24 >> 8) & 0xFF);
                        pcm24_buf[k * 3 + 2] = (uint8_t)((val24 >> 16) & 0xFF);
                    }
                    fifo_push(&fifo, pcm24_buf, total_items * 3);
                } else {
                    fifo_push(&fifo, outbuf, bytes_written);
                }

                uint8_t pop_buf[4096];
                while (fifo.fill > 0) {
                    uint32_t chunk = fifo.fill < sizeof(pop_buf) ? fifo.fill : sizeof(pop_buf);
                    fifo_pop(&fifo, pop_buf, chunk);
                    fwrite(pop_buf, 1, chunk, fout);
                    total_pcm_bytes += chunk;
                }
            }

            frames_decoded++;
            offset += bytes_consumed;
        }
    }

    if (fout) {
        if (is_mp4 && gapless && track.padding > 0) {
            uint32_t padding_bytes = track.padding * num_channels * (bit_depth / 8);
            if (fifo.fill > padding_bytes) {
                fifo_truncate_tail(&fifo, padding_bytes);
            } else {
                fifo.fill = 0;
            }
        }
        uint8_t pop_buf[4096];
        while (fifo.fill > 0) {
            uint32_t chunk = fifo.fill < sizeof(pop_buf) ? fifo.fill : sizeof(pop_buf);
            fifo_pop(&fifo, pop_buf, chunk);
            fwrite(pop_buf, 1, chunk, fout);
            total_pcm_bytes += chunk;
        }
    }
    fifo_free(&fifo);

    double duration_sec = (double)(frames_decoded * 1024) / (sample_rate ? sample_rate : 44100);
    double avg_bitrate_kbps = (file_len * 8.0) / (duration_sec > 0 ? duration_sec * 1000.0 : 1.0);

    if (json_info) {
        printf("{\n");
        printf("  \"file\": \"%s\",\n", infile);
        printf("  \"container\": \"%s\",\n", is_mp4 ? "MP4 / M4A" : "ADTS Bitstream");
        printf("  \"major_brand\": \"%s\",\n", track.major_brand[0] ? track.major_brand : "M4A");
        printf("  \"duration_seconds\": %.2f,\n", duration_sec);
        printf("  \"audio\": {\n");
        printf("    \"profile\": \"%s\",\n", (obj_type == FAAD_OBJ_HE_AAC_V1) ? "HE-AAC v1 (AAC-LC + SBR)" : "AAC-LC");
        printf("    \"channels\": %u,\n", num_channels);
        printf("    \"sample_rate_hz\": %u,\n", sample_rate);
        printf("    \"bitrate_avg_kbps\": %.1f,\n", avg_bitrate_kbps);
        printf("    \"total_frames\": %u\n", frames_decoded);
        printf("  },\n");
        printf("  \"metadata\": {\n");
        printf("    \"encoder\": \"%s\"\n", track.encoder_tag[0] ? track.encoder_tag : "FAAC");
        printf("  }\n");
        printf("}\n");
    } else if (info_only) {
        printf("File:        %s\n", infile);
        printf("Container:   %s (Major Brand: %s)\n", is_mp4 ? "MP4 / M4A" : "ADTS Bitstream", track.major_brand[0] ? track.major_brand : "M4A");
        printf("Duration:    %02d:%02d:%02d.%02d (%.2f seconds)\n\n",
               (int)duration_sec / 3600, ((int)duration_sec % 3600) / 60, (int)duration_sec % 60, (int)(duration_sec * 100) % 100, duration_sec);
        printf("Audio Stream:\n");
        printf("  Profile:   %s\n", (obj_type == FAAD_OBJ_HE_AAC_V1) ? "HE-AAC v1 (AAC-LC + SBR)" : "AAC-LC");
        printf("  Channels:  %u (%s)\n", num_channels, (num_channels == 1) ? "Mono" : ((num_channels == 2) ? "Stereo" : "Multichannel"));
        printf("  Sample Rate: %.1f kHz\n", sample_rate / 1000.0f);
        printf("  Bitrate:   %.1f kbps (Avg)\n", avg_bitrate_kbps);
        printf("  Frames:    %u frames\n\n", frames_decoded);
        printf("Metadata (Tags):\n");
        printf("  Encoder:   %s\n", track.encoder_tag[0] ? track.encoder_tag : "FAAC");
    } else if (fout) {
        if (!raw_format && fout != stdout) {
            write_wav_header(fout, sample_rate, (uint16_t)num_channels, total_pcm_bytes, bit_depth, is_float);
            fclose(fout);
        }
        if (!quiet) {
            printf("Decoded %u frames (%u bytes, %d-bit %s) to %s\n",
                   frames_decoded, total_pcm_bytes, bit_depth, is_float ? "float" : "PCM", outfile ? outfile : "stdout");
        }
    }

    faad_decoder_close(&dec);
    free(inbuf);
    if (is_mp4) mp4_free_track(&track);
    return 0;
}
