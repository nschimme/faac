/*
 * libFuzzer harness for the faad frontend's MP4 reading path.
 *
 * frontend/mp4read.h mp4_read_track_buf() -> decoder init from the ASC ->
 * decode every sample (offset/size from MP4Track.samples, bounds-checked against the input) -> mp4_free_track.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "mp4read.h"
#include "faad.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 8)
        return 0;

    MP4Track track;
    memset(&track, 0, sizeof(track));

    if (!mp4_read_track_buf(data, (long)size, &track)) {
        mp4_free_track(&track);
        return 0;
    }

    if (!track.asc_buf || !track.asc_len || !track.num_samples || !track.samples) {
        mp4_free_track(&track);
        return 0;
    }

    faad_config cfg;
    faad_config_init(&cfg, sizeof(cfg));
    cfg.stream_format = FAAD_STREAM_RAW;
    cfg.output_format = FAAD_OUTPUT_16BIT;

    faad_decoder *dec = NULL;
    if (faad_decoder_open(&cfg, track.asc_buf, track.asc_len, &dec) != FAAD_OK || !dec) {
        mp4_free_track(&track);
        return 0;
    }

    faad_stream_info info;
    info.struct_size = sizeof(info);
    (void)faad_decoder_get_info(dec, &info);

    uint8_t pcm[16384];
    uint32_t limit = track.num_samples > 256 ? 256 : track.num_samples;

    for (uint32_t i = 0; i < limit; i++) {
        uint64_t offset = track.samples[i].offset;
        uint32_t sample_size = track.samples[i].size;

        if (offset >= size || sample_size > size || offset + sample_size > size) {
            continue;
        }

        uint32_t consumed = 0, written = 0, flags = 0;
        (void)faad_decode_frame(dec, data + offset, sample_size, &consumed, pcm, sizeof(pcm), &written, &flags);
    }

    faad_decoder_close(&dec);
    mp4_free_track(&track);
    return 0;
}
