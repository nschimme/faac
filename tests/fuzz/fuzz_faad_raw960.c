/*
 * libFuzzer harness for libfaad's RAW 960-sample (DAB+) decoder API.
 * Input: selector, then little-endian u16 length-prefixed access units.
 * Selector bit 7: instead read a u8 ASC length, ASC bytes, then access units.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "faad3.h"

/* ISO/IEC 14496-3 AudioSpecificConfig: audioObjectType(5),
 * samplingFrequencyIndex(4), channelConfiguration(4). AOT 5/29 additionally
 * has extensionSamplingFrequencyIndex(4), core audioObjectType(5).
 * GASpecificConfig starts with frameLengthFlag=1, dependsOnCoreCoder=0,
 * extensionFlag=0 (100); unused trailing bits are zero.
 * HE uses a 24 kHz core and 48 kHz output; PS uses a mono core.
 */
static const uint8_t asc[][4] = {
    { 0x12, 0x8c },             /* LC, 32 kHz, mono */
    { 0x11, 0x94 },             /* LC, 48 kHz, stereo */
    { 0x2b, 0x11, 0x8a, 0x00 }, /* SBR, 24 -> 48 kHz, stereo */
    { 0xeb, 0x09, 0x8a, 0x00 }, /* PS, 24 -> 48 kHz, mono core */
};

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 2)
        return 0;

    faad_config cfg;
    faad_config_init(&cfg, sizeof(cfg));
    cfg.stream_format = FAAD_STREAM_RAW;
    cfg.output_format = FAAD_OUTPUT_16BIT;

    uint8_t mode = data[0];
    const uint8_t *config = asc[mode & 3];
    uint32_t config_len = (mode & 3) < 2 ? 2 : 4;
    size_t pos = 1;

    if (mode & 0x80) {
        config_len = data[pos++];
        if (config_len > size - pos)
            return 0;
        config = data + pos;
        pos += config_len;
    }

    faad_decoder *dec = NULL;
    if (faad_decoder_open(&cfg, config, config_len, &dec) != FAAD_OK || !dec)
        return 0;

    faad_stream_info info;
    info.struct_size = sizeof(info);
    (void)faad_decoder_get_info(dec, &info);

    uint8_t pcm[16384];
    int frames = 0;

    while (size - pos >= 2 && frames < 128) {
        uint32_t len = (uint32_t)data[pos] | ((uint32_t)data[pos + 1] << 8);
        pos += 2;
        if (len > size - pos)
            break;

        uint32_t consumed = 0;
        uint32_t written = 0;
        uint32_t flags = 0;
        faad_status st = faad_decode_frame(dec, data + pos, len,
                               &consumed, pcm, sizeof(pcm), &written, &flags);

        if (flags & FAAD_FRAME_FORMAT_CHANGED) {
            info.struct_size = sizeof(info);
            (void)faad_decoder_get_info(dec, &info);
        }

        if (st != FAAD_OK && st != FAAD_ERR_NEED_MORE_DATA && st != FAAD_ERR_SYNC_LOST && st != FAAD_ERR_DECODE_FAILED)
            break;

        pos += len;
        frames++;
        if (frames % 32 == 0)
            (void)faad_decoder_flush(dec);
    }

    faad_decoder_close(&dec);
    return 0;
}
