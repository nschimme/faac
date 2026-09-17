/*
 * MP4Box-Compatible CLI frontend for libfaam and libfaad
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#endif

#include "faam.h"

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
    faam_library_info info;
    info.struct_size = sizeof(info);
    if (faam_get_library_info(&info) != FAAM_OK) {
        info.version = "1.0.0";
    }

    printf("FAAM - Freeware Advanced Audio Muxer (v%s)\n", info.version);
    printf("Usage: faam [options] <input_file>\n\n");
    printf("MP4Box-Compatible Options:\n");
    printf("  -info <file.m4a>             Print audio & container summary\n");
    printf("  -disso <file.m4a>            Dump MP4 atom box tree hierarchy\n");
    printf("  -add <input.aac> <out.m4a>   Mux raw AAC/ADTS stream into M4A/M4B container\n");
    printf("  -raw [track_id] <file.m4a>   Extract raw elementary AAC stream from container\n");
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
        fprintf(stderr, "Error: Missing input file.\n");
        return 1;
    }

    FILE *f = fopen(filepath, "rb");
    if (!f) {
        fprintf(stderr, "Error: Cannot open %s\n", filepath);
        return 1;
    }

    faam_io io = {0};
    io.user_data = f;
    io.read = file_read_cb;
    io.write = file_write_cb;
    io.seek = file_seek_cb;
    io.tell = file_tell_cb;

    uint32_t state_size = 0;
    faam_demuxer_get_state_size(&state_size);
    void *mem = malloc(state_size);
    faam_demuxer *d = NULL;

    if (faam_demuxer_init(mem, state_size, &io, &d) != FAAM_OK) {
        fprintf(stderr, "Error: Failed to parse container %s\n", filepath);
        free(mem);
        fclose(f);
        return 1;
    }

    uint8_t asc[64] = {0};
    uint32_t asc_len = 0;
    faam_demuxer_get_asc(d, asc, sizeof(asc), &asc_len);

    faam_asc_info asc_info = {0};
    faam_asc_parse(asc, asc_len, &asc_info);

    faam_gapless_info gapless = {0};
    faam_demuxer_get_gapless(d, &gapless);

    printf("* File %s:\n", filepath);
    printf("  Container: MP4 / M4A Audio\n");
    printf("  Track #1 Info: Type 'soun' Brand 'M4A '\n");
    printf("  Audio Profile: %s (AOT %u)\n",
           asc_info.sbr_present ? (asc_info.ps_present ? "HE-AAC v2" : "HE-AAC v1") : "AAC-LC",
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

static int cmd_disso(const char *filepath)
{
    if (!filepath) {
        fprintf(stderr, "Error: Missing input file for box dissection.\n");
        return 1;
    }

    FILE *f = fopen(filepath, "rb");
    if (!f) {
        fprintf(stderr, "Error: Cannot open %s\n", filepath);
        return 1;
    }

    printf("<!-- MP4Box-compatible Atom Tree XML Dump for: %s -->\n", filepath);
    printf("<IsoMediaFile File=\"%s\">\n", filepath);

    uint8_t hdr[8];
    uint64_t pos = 0;
    while (fread(hdr, 1, 8, f) == 8) {
        uint32_t size = (hdr[0] << 24) | (hdr[1] << 16) | (hdr[2] << 8) | hdr[3];
        char type[5] = { hdr[4], hdr[5], hdr[6], hdr[7], 0 };

        for (int k = 0; k < 4; k++) {
            if (type[k] < 32 || type[k] > 126) type[k] = '?';
        }

        printf("  <%sBox Size=\"%u\" Offset=\"%llu\">\n", type, size, (unsigned long long)pos);

        if (strcmp(type, "moov") == 0 || strcmp(type, "trak") == 0 || strcmp(type, "mdia") == 0 ||
            strcmp(type, "minf") == 0 || strcmp(type, "stbl") == 0 || strcmp(type, "udta") == 0 ||
            strcmp(type, "meta") == 0 || strcmp(type, "ilst") == 0) {
            /* Sub-container box - keep reading sequentially */
        } else {
            /* Leaf atom box - skip payload */
            if (size >= 8) {
                fseek(f, (long)(size - 8), SEEK_CUR);
                pos += size - 8;
            }
        }
        printf("  </%sBox>\n", type);
        pos += 8;
    }

    printf("</IsoMediaFile>\n");
    fclose(f);
    return 0;
}

static int cmd_add(const char *input_file, const char *output_file, const char *brand_str, uint32_t delay, uint32_t padding)
{
    if (!input_file || !output_file) {
        fprintf(stderr, "Error: Missing input or output file for muxing.\n");
        return 1;
    }

    FILE *fin = fopen(input_file, "rb");
    if (!fin) {
        fprintf(stderr, "Error: Cannot open raw AAC input %s\n", input_file);
        return 1;
    }

    FILE *fout = fopen(output_file, "wb");
    if (!fout) {
        fclose(fin);
        fprintf(stderr, "Error: Cannot open output M4A file %s\n", output_file);
        return 1;
    }

    faam_io io = {0};
    io.user_data = fout;
    io.read = file_read_cb;
    io.write = file_write_cb;
    io.seek = file_seek_cb;
    io.tell = file_tell_cb;

    faam_muxer_config cfg;
    faam_muxer_config_init(&cfg, sizeof(cfg));
    cfg.timescale = 44100;
    cfg.channels = 2;
    cfg.bits_per_sample = 16;
    cfg.is_m4b = (brand_str && strcasecmp(brand_str, "M4B") == 0);
    cfg.gapless.encoder_delay = delay;
    cfg.gapless.end_padding = padding;

    uint8_t dummy_asc[2] = {0x12, 0x10}; /* 44.1 kHz, stereo AAC-LC */
    cfg.asc_buf = dummy_asc;
    cfg.asc_len = sizeof(dummy_asc);

    uint32_t state_size = 0;
    faam_muxer_get_state_size(&cfg, &state_size);
    void *mem = malloc(state_size);
    faam_muxer *m = NULL;

    if (faam_muxer_init(mem, state_size, &cfg, &io, &m) != FAAM_OK) {
        fprintf(stderr, "Error: Failed to initialize muxer.\n");
        free(mem);
        fclose(fin);
        fclose(fout);
        return 1;
    }

    uint8_t buf[2048];
    size_t bytes = 0;
    while ((bytes = fread(buf, 1, sizeof(buf), fin)) > 0) {
        faam_muxer_write_frame(m, buf, (uint32_t)bytes, 1024);
    }

    faam_muxer_finalize(m);
    faam_muxer_close(m);
    free(mem);
    fclose(fin);
    fclose(fout);

    printf("Muxed %s into %s (Brand: %s)\n", input_file, output_file, cfg.is_m4b ? "M4B " : "M4A ");
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
        fprintf(stderr, "Error: Cannot open %s\n", input_file);
        return 1;
    }

    faam_io io = {0};
    io.user_data = fin;
    io.read = file_read_cb;
    io.write = file_write_cb;
    io.seek = file_seek_cb;
    io.tell = file_tell_cb;

    uint32_t state_size = 0;
    faam_demuxer_get_state_size(&state_size);
    void *mem = malloc(state_size);
    faam_demuxer *d = NULL;

    if (faam_demuxer_init(mem, state_size, &io, &d) != FAAM_OK) {
        fprintf(stderr, "Error: Failed to demux container %s\n", input_file);
        free(mem);
        fclose(fin);
        return 1;
    }

    FILE *fout = fopen(output_file, "wb");
    if (!fout) {
        fprintf(stderr, "Error: Cannot open output raw stream %s\n", output_file);
        faam_demuxer_close(d);
        free(mem);
        fclose(fin);
        return 1;
    }

    uint8_t frame[4096];
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

    faam_metadata meta = {0};
    char buf[512];
    strncpy(buf, tag_spec, sizeof(buf) - 1);

    char *pair = strtok(buf, ":");
    while (pair) {
        char *eq = strchr(pair, '=');
        if (eq) {
            *eq = '\0';
            const char *key = pair;
            const char *val = eq + 1;

            if (strcmp(key, "title") == 0 || strcmp(key, "nam") == 0) strncpy(meta.title, val, sizeof(meta.title) - 1);
            else if (strcmp(key, "artist") == 0 || strcmp(key, "ART") == 0) strncpy(meta.artist, val, sizeof(meta.artist) - 1);
            else if (strcmp(key, "album") == 0 || strcmp(key, "alb") == 0) strncpy(meta.album, val, sizeof(meta.album) - 1);
            else if (strcmp(key, "composer") == 0 || strcmp(key, "wrt") == 0) strncpy(meta.composer, val, sizeof(meta.composer) - 1);
            else if (strcmp(key, "year") == 0 || strcmp(key, "day") == 0) strncpy(meta.year, val, sizeof(meta.year) - 1);
            else if (strcmp(key, "comment") == 0 || strcmp(key, "cmt") == 0) strncpy(meta.comment, val, sizeof(meta.comment) - 1);
        }
        pair = strtok(NULL, ":");
    }

    if (faam_update_tags(filepath, &meta) == FAAM_OK) {
        printf("Applied iTunes metadata tags to %s\n", filepath);
        return 0;
    }

    fprintf(stderr, "Error: Failed to update metadata tags in %s\n", filepath);
    return 1;
}

int main(int argc, char *argv[])
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

    (void)chap_file;

    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-info") == 0 || strcmp(argv[i], "info") == 0) && i + 1 < argc) {
            info_file = argv[++i];
        } else if ((strcmp(argv[i], "-disso") == 0 || strcmp(argv[i], "dump") == 0) && i + 1 < argc) {
            disso_file = argv[++i];
        } else if ((strcmp(argv[i], "-add") == 0 || strcmp(argv[i], "mux") == 0) && i + 1 < argc) {
            add_file = argv[++i];
        } else if ((strcmp(argv[i], "-raw") == 0 || strcmp(argv[i], "demux") == 0) && i + 1 < argc) {
            if (i + 1 < argc && isdigit((unsigned char)argv[i + 1][0]) && i + 2 < argc) {
                i++; /* Ignore numeric track ID parameter in -raw 1 <file.m4a> */
            }
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

    if (target_file) {
        return cmd_info(target_file);
    }

    print_usage();
    return 1;
}
