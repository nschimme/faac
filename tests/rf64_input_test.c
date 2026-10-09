/* Header and chunk traversal at 64-bit offsets without encoding gigabytes. */
#include "input.h"
#include "cli_io.h"
#include <string.h>

static bool check_float_endian(void)
{
    const uint8_t samples[2][8] = {
        { 0x00, 0x00, 0x80, 0x3e, 0x00, 0x00, 0x00, 0xbf },
        { 0x3e, 0x80, 0x00, 0x00, 0xbf, 0x00, 0x00, 0x00 }
    };
    for (unsigned be = 0; be <= 1; be++) {
        FILE *file = tmpfile();
        if (!file) return false;
        pcmfile_t f = { .f = file, .channels = 2, .samplebytes = 4,
                       .isfloat = true, .bigendian = be };
        float pcm[2];
        bool ok = fwrite(samples[be], 1, 8, file) == 8 && cli_fseek(file, 0) &&
                  wav_read_float32(&f, pcm, 2, NULL) == 2 &&
                  pcm[0] == 8192.0f && pcm[1] == -16384.0f;
        fclose(file);
        if (!ok) return false;
    }
    return true;
}

int main(int argc, char **argv)
{
    if (argc != 4 || !check_float_endian()) return 1;
    pcmfile_t *f = wav_open_read(argv[1], false);
    if (!f) return 1;
    uint64_t expected = strtoull(argv[2], NULL, 10);
    uint64_t offset = strtoull(argv[3], NULL, 10);
    bool ok = f->channels == 2 && f->samplebytes == 2 && f->samplerate == 48000 &&
              f->samples == (int64_t)(expected / 4) && cli_ftell(f->f) == offset;
    uint8_t data[4];
    ok = ok && wav_read_native(f, data, 2) == 2 && !memcmp(data, "\1\2\3\4", 4);
    wav_close(f);
    return ok ? 0 : 1;
}
