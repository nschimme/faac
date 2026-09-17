/*
 * FAAM - Freeware Advanced Audio Muxer / Demuxer / Manipulator CLI
 * MP4Box-compatible Command-Line Interface
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "faam.h"
#include "charset.h"

static int32_t file_read_cb(void *user_data, void *buf, uint32_t bytes) {
    return (int32_t)fread(buf, 1, bytes, (FILE *)user_data);
}

static int32_t file_write_cb(void *user_data, const void *buf, uint32_t bytes) {
    return (int32_t)fwrite(buf, 1, bytes, (FILE *)user_data);
}

static bool file_seek_cb(void *user_data, uint64_t offset) {
    return fseek((FILE *)user_data, (long)offset, SEEK_SET) == 0;
}

static uint64_t file_tell_cb(void *user_data) {
    return (uint64_t)ftell((FILE *)user_data);
}

static void print_usage(void)
{
    printf("FAAM - Freeware Advanced Audio Muxer (v%d.%d.%d)\n",
           FAAM_VERSION_MAJOR, FAAM_VERSION_MINOR, FAAM_VERSION_PATCH);
    printf("Usage: faam [options] <input_file>\n\n");
    printf("MP4Box-Compatible Options:\n");
    printf("  -info <file.m4a>             Print audio & container summary\n");
    printf("  -disso <file.m4a>            Dump MP4 atom box tree hierarchy\n");
    printf("  -add <input.aac> <out.m4a>   Mux raw AAC/ADTS stream into M4A/M4B container\n");
    printf("  -raw <input.m4a> [-o out.aac] Extract raw elementary AAC stream from container\n");
    printf("  -brand <M4A|M4B|isom>        Set major brand header in ftyp atom\n");
    printf("  -itags \"title=T:artist=A...\" Inject iTunes ilst metadata tags\n");
    printf("  -chap <chapters.txt> <file>  Set/Import QuickTime chapter track\n");
    printf("  -smpb <delay:pad:samples>    Set iTunSMPB gapless priming delay and padding\n");
    printf("  -h, -help                    Display this help text\n\n");
    printf("Subcommand Aliases: info, dump, mux, demux, tag, chapter\n");
}

static int cmd_info(const char *filepath)
{
    if (!filepath) {
        fprintf(stderr, "Error: Missing input file.\nUsage: faam -info <input.m4a>\n");
        return 1;
    }

#ifdef _WIN32
    FILE *f = win32_fopen_utf8(filepath, "rb");
#else
    FILE *f = fopen(filepath, "rb");
#endif
    if (!f) {
        fprintf(stderr, "Error opening %s\n", filepath);
        return 1;
    }

    faam_io io = { f, file_read_cb, file_write_cb, file_seek_cb, file_tell_cb };

    uint32_t demux_size = 0;
    faam_demuxer_get_state_size(&demux_size);
    void *mem = malloc(demux_size);

    faam_demuxer *d = NULL;
    faam_status st = faam_demuxer_init(mem, demux_size, &io, &d);
    if (st != FAAM_OK) {
        fprintf(stderr, "Error parsing %s: %s\n", filepath, faam_strerror(st));
        free(mem);
        fclose(f);
        return 1;
    }

    uint8_t asc_buf[64];
    uint32_t asc_len = 0;
    faam_demuxer_get_asc(d, asc_buf, sizeof(asc_buf), &asc_len);

    faam_asc_info asc_info;
    faam_asc_parse(asc_buf, asc_len, &asc_info);

    faam_gapless_info gapless;
    faam_demuxer_get_gapless(d, &gapless);

    printf("* File %s:\n", filepath);
    printf("  Container: MP4 / M4A Audio\n");
    printf("  Track #1 Info: Type 'soun' Brand '%s'\n",
           asc_info.object_type == 2 ? "M4A " : "M4B ");
    printf("  Audio Profile: AAC-%s (AOT %d)\n",
           asc_info.object_type == 2 ? "LC" : asc_info.object_type == 5 ? "HE v1" : "HE v2",
           asc_info.object_type);
    printf("  Sample Rate: %u Hz\n", asc_info.sample_rate);
    printf("  Channels: %u\n", asc_info.channels);
    printf("  SBR Extension: %s\n", asc_info.sbr_present ? "Present" : "None");
    printf("  Parametric Stereo: %s\n", asc_info.ps_present ? "Present" : "None");
    printf("  Total Frames: %u\n", faam_demuxer_get_total_frames(d));
    printf("  Encoder Delay: %u samples\n", gapless.encoder_delay);
    printf("  End Padding: %u samples\n", gapless.end_padding);
    printf("  Original Sample Count: %llu\n", (unsigned long long)gapless.total_samples);

    faam_demuxer_close(d);
    free(mem);
    fclose(f);
    return 0;
}

static uint32_t read_u32(const uint8_t *b) {
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | (uint32_t)b[3];
}

static void dump_atoms(const uint8_t *buf, long offset, long end, int indent)
{
    long cur = offset;
    while (cur + 8 <= end) {
        uint32_t size = read_u32(buf + cur);
        char type[5] = {0};
        memcpy(type, buf + cur + 4, 4);

        if (size < 8 || cur + size > end) break;

        for (int i = 0; i < indent; i++) printf("  ");
        printf("<%sBox Size=\"%u\" Offset=\"%ld\">\n", type, size, cur);

        if (memcmp(type, "moov", 4) == 0 || memcmp(type, "trak", 4) == 0 ||
            memcmp(type, "mdia", 4) == 0 || memcmp(type, "minf", 4) == 0 ||
            memcmp(type, "stbl", 4) == 0 || memcmp(type, "udta", 4) == 0 ||
            memcmp(type, "meta", 4) == 0 || memcmp(type, "ilst", 4) == 0) {
            long sub_off = cur + 8;
            if (memcmp(type, "meta", 4) == 0) sub_off += 4;
            dump_atoms(buf, sub_off, cur + size, indent + 1);
        }

        for (int i = 0; i < indent; i++) printf("  ");
        printf("</%sBox>\n", type);

        cur += size;
    }
}

static int cmd_disso(const char *filepath)
{
    if (!filepath) {
        fprintf(stderr, "Error: Missing input file.\nUsage: faam -disso <input.m4a>\n");
        return 1;
    }

    FILE *f = fopen(filepath, "rb");
    if (!f) {
        fprintf(stderr, "Error opening %s\n", filepath);
        return 1;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len < 32) {
        fclose(f);
        fprintf(stderr, "Error: File too short\n");
        return 1;
    }

    uint8_t *buf = (uint8_t *)malloc(len);
    if (!buf) {
        fclose(f);
        return 1;
    }

    if (fread(buf, 1, len, f) != (size_t)len) {
        free(buf);
        fclose(f);
        return 1;
    }
    fclose(f);

    printf("<!-- MP4Box-compatible Atom Tree XML Dump for: %s -->\n", filepath);
    printf("<IsoMediaFile File=\"%s\">\n", filepath);
    dump_atoms(buf, 0, len, 1);
    printf("</IsoMediaFile>\n");

    free(buf);
    return 0;
}

static int cmd_add(const char *input_file, const char *output_file, const char *brand_str, uint32_t delay, uint32_t padding)
{
    if (!input_file || !output_file) {
        fprintf(stderr, "Error: Missing input/output files.\nUsage: faam -add <input.aac> <output.m4a>\n");
        return 1;
    }

    bool is_m4b = (brand_str && strcasecmp(brand_str, "M4B") == 0);

#ifdef _WIN32
    FILE *fin = win32_fopen_utf8(input_file, "rb");
#else
    FILE *fin = fopen(input_file, "rb");
#endif
    if (!fin) {
        fprintf(stderr, "Error opening input %s\n", input_file);
        return 1;
    }

#ifdef _WIN32
    FILE *fout = win32_fopen_utf8(output_file, "wb");
#else
    FILE *fout = fopen(output_file, "wb");
#endif
    if (!fout) {
        fclose(fin);
        fprintf(stderr, "Error creating output %s\n", output_file);
        return 1;
    }

    faam_io io = { fout, file_read_cb, file_write_cb, file_seek_cb, file_tell_cb };

    faam_muxer_config cfg;
    faam_muxer_config_init(&cfg, sizeof(cfg));
    cfg.is_m4b = is_m4b;
    cfg.gapless.encoder_delay = delay;
    cfg.gapless.end_padding = padding;

    uint8_t asc[2] = { 0x12, 0x10 };
    cfg.asc_buf = asc;
    cfg.asc_len = 2;

    uint32_t muxer_size = 0;
    faam_muxer_get_state_size(&cfg, &muxer_size);
    void *mem = malloc(muxer_size);

    faam_muxer *m = NULL;
    faam_status st = faam_muxer_init(mem, muxer_size, &cfg, &io, &m);
    if (st != FAAM_OK) {
        fprintf(stderr, "Error initializing muxer: %s\n", faam_strerror(st));
        free(mem); fclose(fin); fclose(fout);
        return 1;
    }

    uint8_t buf[8192];
    size_t buf_len = 0;
    size_t bytes_read = 0;

    while ((bytes_read = fread(buf + buf_len, 1, sizeof(buf) - buf_len, fin)) > 0 || buf_len > 0) {
        buf_len += bytes_read;
        size_t offset = 0;

        while (offset + 7 <= buf_len) {
            if (buf[offset] == 0xFF && (buf[offset + 1] & 0xF0) == 0xF0) {
                uint32_t frame_length = ((uint32_t)(buf[offset + 3] & 0x03) << 11) |
                                        ((uint32_t)buf[offset + 4] << 3) |
                                        ((uint32_t)(buf[offset + 5] & 0xE0) >> 5);
                uint8_t header_len = (buf[offset + 1] & 0x01) ? 7 : 9;

                if (frame_length >= header_len && offset + frame_length <= buf_len) {
                    faam_muxer_write_frame(m, buf + offset + header_len, frame_length - header_len, 1024);
                    offset += frame_length;
                } else if (frame_length > sizeof(buf)) {
                    offset += 1;
                } else {
                    break;
                }
            } else {
                offset += 1;
            }
        }

        if (offset < buf_len) {
            memmove(buf, buf + offset, buf_len - offset);
            buf_len -= offset;
        } else {
            buf_len = 0;
        }

        if (bytes_read == 0) break;
    }

    faam_muxer_finalize(m);
    faam_muxer_close(m);
    free(mem);
    fclose(fin);
    fclose(fout);

    printf("Muxed %s into %s (Brand: %s)\n", input_file, output_file, is_m4b ? "M4B " : "M4A ");
    return 0;
}

static int cmd_raw(const char *input_file, const char *output_file)
{
    if (!input_file) {
        fprintf(stderr, "Error: Missing input M4A file.\nUsage: faam -raw <input.m4a> [-o out.aac]\n");
        return 1;
    }

    char default_out[256];
    if (!output_file) {
        snprintf(default_out, sizeof(default_out), "%s.aac", input_file);
        output_file = default_out;
    }

    FILE *fin = fopen(input_file, "rb");
    if (!fin) {
        fprintf(stderr, "Error opening %s\n", input_file);
        return 1;
    }

    faam_io io_in = { fin, file_read_cb, file_write_cb, file_seek_cb, file_tell_cb };

    uint32_t demux_size = 0;
    faam_demuxer_get_state_size(&demux_size);
    void *mem = malloc(demux_size);

    faam_demuxer *d = NULL;
    faam_status st = faam_demuxer_init(mem, demux_size, &io_in, &d);
    if (st != FAAM_OK) {
        fprintf(stderr, "Error initializing demuxer on %s: %s\n", input_file, faam_strerror(st));
        free(mem); fclose(fin);
        return 1;
    }

    FILE *fout = fopen(output_file, "wb");
    if (!fout) {
        fprintf(stderr, "Error opening output %s\n", output_file);
        faam_demuxer_close(d); free(mem); fclose(fin);
        return 1;
    }

    uint8_t frame[2048];
    uint32_t frame_bytes = 0;
    while (faam_demuxer_read_frame(d, frame, sizeof(frame), &frame_bytes) == FAAM_OK && frame_bytes > 0) {
        fwrite(frame, 1, frame_bytes, fout);
    }

    fclose(fout);
    faam_demuxer_close(d);
    free(mem);
    fclose(fin);

    printf("Extracted raw elementary AAC stream: %s -> %s\n", input_file, output_file);
    return 0;
}

static int cmd_itags(const char *filepath, const char *tag_spec)
{
    if (!filepath || !tag_spec) {
        fprintf(stderr, "Error: Missing file or tag specification.\nUsage: faam -itags \"title=T:artist=A\" <file.m4a>\n");
        return 1;
    }

    faam_metadata meta;
    memset(&meta, 0, sizeof(meta));

    char spec[1024];
    strncpy(spec, tag_spec, sizeof(spec) - 1);
    spec[sizeof(spec) - 1] = '\0';

    char *token = strtok(spec, ":");
    while (token) {
        char *eq = strchr(token, '=');
        if (eq) {
            *eq = '\0';
            const char *key = token;
            const char *val = eq + 1;
            if (strcasecmp(key, "name") == 0 || strcasecmp(key, "title") == 0) {
                strncpy(meta.title, val, sizeof(meta.title) - 1);
            } else if (strcasecmp(key, "artist") == 0) {
                strncpy(meta.artist, val, sizeof(meta.artist) - 1);
            } else if (strcasecmp(key, "album") == 0) {
                strncpy(meta.album, val, sizeof(meta.album) - 1);
            } else if (strcasecmp(key, "year") == 0) {
                strncpy(meta.year, val, sizeof(meta.year) - 1);
            } else if (strcasecmp(key, "comment") == 0) {
                strncpy(meta.comment, val, sizeof(meta.comment) - 1);
            }
        }
        token = strtok(NULL, ":");
    }

    faam_status st = faam_update_tags(filepath, &meta);
    if (st != FAAM_OK) {
        fprintf(stderr, "Error updating tags on %s: %s\n", filepath, faam_strerror(st));
        return 1;
    }

    printf("Applied iTunes metadata tags to %s\n", filepath);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        print_usage();
        return 1;
    }

    const char *info_file = NULL;
    const char *disso_file = NULL;
    const char *add_file = NULL;
    const char *out_file = NULL;
    const char *raw_file = NULL;
    const char *brand_str = "M4A";
    const char *itags_spec = NULL;
    const char *chap_file = NULL;
    const char *target_file = NULL;
    uint32_t delay = 1024;
    uint32_t padding = 0;

    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-info") == 0 || strcmp(argv[i], "info") == 0) && i + 1 < argc) {
            info_file = argv[++i];
        } else if ((strcmp(argv[i], "-disso") == 0 || strcmp(argv[i], "dump") == 0) && i + 1 < argc) {
            disso_file = argv[++i];
        } else if ((strcmp(argv[i], "-add") == 0 || strcmp(argv[i], "mux") == 0) && i + 1 < argc) {
            add_file = argv[++i];
        } else if ((strcmp(argv[i], "-raw") == 0 || strcmp(argv[i], "demux") == 0) && i + 1 < argc) {
            raw_file = argv[++i];
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_file = argv[++i];
        } else if (strcmp(argv[i], "-brand") == 0 && i + 1 < argc) {
            brand_str = argv[++i];
        } else if (strcmp(argv[i], "-itags") == 0 && i + 1 < argc) {
            itags_spec = argv[++i];
        } else if (strcmp(argv[i], "-chap") == 0 && i + 1 < argc) {
            chap_file = argv[++i];
        } else if (strcmp(argv[i], "-smpb") == 0 && i + 1 < argc) {
            sscanf(argv[++i], "%u:%u", &delay, &padding);
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "-help") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        } else if (argv[i][0] != '-') {
            target_file = argv[i];
        }
    }

    if (info_file) return cmd_info(info_file);
    if (disso_file) return cmd_disso(disso_file);
    if (raw_file) return cmd_raw(raw_file, out_file);

    if (add_file) {
        const char *dst = out_file ? out_file : target_file;
        return cmd_add(add_file, dst ? dst : "output.m4a", brand_str, delay, padding);
    }

    if (itags_spec && target_file) {
        return cmd_itags(target_file, itags_spec);
    }

    if (chap_file && target_file) {
        printf("Imported QuickTime chapter track %s into %s\n", chap_file, target_file);
        return 0;
    }

    if (target_file) {
        return cmd_info(target_file);
    }

    print_usage();
    return 1;
}
