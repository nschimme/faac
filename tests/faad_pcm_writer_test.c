/* Exercise production PCM finalization across signed and unsigned 32-bit
 * boundaries using sparse files, without decoding gigabytes of audio. */
#define main faad_cli_main
#include "../frontend/faad_main.c"
#undef main
#ifdef _WIN32
#include <winioctl.h>
#endif

static int check_size(const char *path, uint64_t pcm_bytes, bool raw, bool gapless, bool expected_ok)
{
    PCMWriter w;
    if (!pcm_writer_open(&w, path, false, raw, 16, false, true, gapless, 0, 1920)) return 1;
#ifdef _WIN32
    /* NTFS needs an explicit sparse-file flag to avoid allocating gigabytes.
     * Skip on a filesystem that cannot provide sparse test fixtures. */
    DWORD returned;
    HANDLE handle = (HANDLE)_get_osfhandle(_fileno(w.fout));
    if (!DeviceIoControl(handle, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &returned, NULL)) {
        pcm_writer_close(&w);
        return 77;
    }
#endif
    w.gapless_scaled = true;
    uint32_t rate = 48000, channels = 2;
    enum faad_object_type object = FAAD_OBJ_LC;
    faad_stream_info info = { .struct_size = sizeof(info), .sample_rate = 48000, .channels = 2 };
    MP4Track track = {0};
    uint8_t sample[4] = {1, 2, 3, 4};
    if (!pcm_writer_write_frame(&w, NULL, &info, &track, 0, sample, sizeof(sample),
                                &rate, &channels, &object)) goto fail;
    uint64_t header_bytes = raw ? 0 : 80;
    if (!cli_fseek(w.fout, header_bytes + pcm_bytes - sizeof(sample))) goto fail;
    w.total_pcm_bytes = pcm_bytes - sizeof(sample);
    if (!pcm_writer_write_frame(&w, NULL, &info, &track, 0, sample, sizeof(sample),
                                &rate, &channels, &object)) goto fail;
    bool ok = pcm_writer_finish(&w, rate, channels);
    pcm_writer_close(&w);
    if (ok != expected_ok) return 1;
    if (!ok) return 0;
    uint64_t expected_pcm = pcm_bytes - (gapless ? 1920 * 4 : 0);
    if (w.total_pcm_bytes != expected_pcm) return 1;
    FILE *f = cli_fopen(path, "rb");
    if (!f) return 1;
    uint64_t length;
    bool valid = cli_fsize(f, &length) && length == header_bytes + expected_pcm;
    if (valid && !raw) {
        uint32_t riff_size, data_size;
        char magic[4], chunk[4];
        uint64_t sizes[3];
        bool rf64 = expected_pcm + 72 > UINT32_MAX;
        valid = fread(magic, 1, 4, f) == 4 &&
                fread(&riff_size, 4, 1, f) == 1 &&
                cli_fseek(f, 12) && fread(chunk, 1, 4, f) == 4 &&
                cli_fseek(f, 20) && fread(sizes, 8, 3, f) == 3 &&
                cli_fseek(f, 76) && fread(&data_size, 4, 1, f) == 1;
        valid = valid && !memcmp(magic, rf64 ? "RF64" : "RIFF", 4) &&
                !memcmp(chunk, rf64 ? "ds64" : "JUNK", 4) &&
                le32toh(riff_size) == (rf64 ? UINT32_MAX : expected_pcm + 72) &&
                le32toh(data_size) == (rf64 ? UINT32_MAX : expected_pcm);
        if (rf64) valid = valid && le64toh(sizes[0]) == expected_pcm + 72 &&
            le64toh(sizes[1]) == expected_pcm && le64toh(sizes[2]) == expected_pcm / 4;
    }
    fclose(f);
    return valid ? 0 : 1;
fail:
    pcm_writer_close(&w);
    return 1;
}

static int check_headers(const char *path)
{
    const uint16_t channels[] = { 1, 2, 6, 8 };
    const uint16_t bits[] = { 16, 24, 32 };
    for (size_t c = 0; c < sizeof(channels) / sizeof(channels[0]); c++) {
        for (size_t b = 0; b < sizeof(bits) / sizeof(bits[0]); b++) {
            for (unsigned floating = 0; floating <= (bits[b] == 32); floating++) {
                FILE *f = cli_fopen(path, "wb+");
                if (!f) return 1;
                uint64_t bytes = ((UINT64_C(1) << 32) / (channels[c] * (bits[b] / 8)) + 1) *
                                 channels[c] * (bits[b] / 8);
                bool ok = write_wav_header(f, 48000, channels[c], bytes, bits[b], floating);
                uint8_t header[104];
                size_t length = channels[c] > 2 ? 104 : 80;
                ok = ok && cli_fseek(f, 0) && fread(header, 1, length, f) == length;
                uint64_t sizes[3];
                memcpy(sizes, header + 20, sizeof(sizes));
                uint16_t tag;
                memcpy(&tag, header + 56, 2);
                ok = ok && !memcmp(header, "RF64", 4) && !memcmp(header + 12, "ds64", 4) &&
                     le64toh(sizes[0]) == bytes + length - 8 + (bytes & 1) &&
                     le64toh(sizes[1]) == bytes &&
                     le64toh(sizes[2]) == bytes / (channels[c] * (bits[b] / 8)) &&
                     le16toh(tag) == (channels[c] > 2 ? 0xfffe : floating ? 3 : 1);
                if (channels[c] > 2) ok = ok && header[80] == (floating ? 3 : 1);
                fclose(f);
                if (!ok) return 1;
            }
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    uint64_t sizes[] = { (UINT64_C(1) << 31) - 4, (UINT64_C(1) << 31) + 4,
                         (UINT64_C(1) << 32) - 76, (UINT64_C(1) << 32) - 72,
                         (UINT64_C(1) << 32) - 40, (UINT64_C(1) << 32) + 32768,
                         UINT64_C(5) << 30 };
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        for (unsigned gapless = 0; gapless <= 1; gapless++) {
            int status = check_size(argv[1], sizes[i], true, gapless, true);
            if (status) return status;
            status = check_size(argv[1], sizes[i], false, gapless, true);
            if (status) return status;
        }
    }
    return check_headers(argv[1]);
}
