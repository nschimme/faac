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
    uint64_t header_bytes = raw ? 0 : 44;
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
        valid = cli_fseek(f, 4) && fread(&riff_size, 4, 1, f) == 1 &&
                cli_fseek(f, 40) && fread(&data_size, 4, 1, f) == 1 &&
                le32toh(riff_size) == expected_pcm + 36 && le32toh(data_size) == expected_pcm;
    }
    fclose(f);
    return valid ? 0 : 1;
fail:
    pcm_writer_close(&w);
    return 1;
}

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    uint64_t sizes[] = { (UINT64_C(1) << 31) - 4, (UINT64_C(1) << 31) + 4,
                         (UINT64_C(1) << 32) - 40, (UINT64_C(1) << 32) + 32768,
                         UINT64_C(5) << 30 };
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        for (unsigned gapless = 0; gapless <= 1; gapless++) {
            int status = check_size(argv[1], sizes[i], true, gapless, true);
            if (status) return status;
            status = check_size(argv[1], sizes[i], false, gapless, sizes[i] <= UINT32_MAX - 36u);
            if (status) return status;
        }
    }
    return 0;
}
