/*
 * libFuzzer harness for the frontend's PCM reader: the input is a whole file
 * (WAV header and chunks, or raw PCM), handed to wav_open_read() through a
 * memfd and drained with wav_read_float32().
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>

#include "input.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    char path[64];
    float buf[4096];
    pcmfile_t *f;
    int *map = NULL;
    int fd, raw;
    size_t n, total = 0;

    if (size < 2)
        return 0;
    raw = data[0] & 1;
    fd = memfd_create("fuzz", 0);
    if (fd < 0)
        return 0;
    if (write(fd, data + 1, size - 1) != (ssize_t)(size - 1)) {
        close(fd);
        return 0;
    }
    snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);

    f = wav_open_read(path, raw);
    if (f) {
        if (raw) {
            f->channels = 1 + (data[0] >> 1 & 7);
            f->samplebytes = 1 + (data[0] >> 4 & 3);
            f->bigendian = data[0] >> 6 & 1;
        } else if (f->channels >= 3) {
            map = mk_chan_map(f->channels, data[0] >> 1 & 7, data[0] >> 4 & 7);
        }
        while ((n = wav_read_float32(f, buf, sizeof(buf) / sizeof(buf[0]) / (f->channels ? f->channels : 1) * f->channels, map)) > 0 && total < (1u << 20))
            total += n;
        wav_close(f);
    }
    free(map);
    close(fd);
    return 0;
}
