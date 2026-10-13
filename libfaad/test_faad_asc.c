/* AAC-LC AudioSpecificConfig forms that must open a decoder. */
#include <stdio.h>
#include <stdint.h>
#include "faad.h"

static int failures;

static void check(const char *name, const uint8_t *asc, uint32_t len,
                  faad_status want, uint32_t want_rate)
{
    faad_config cfg;
    faad_decoder *dec = NULL;
    faad_config_init(&cfg, sizeof(cfg));
    cfg.stream_format = FAAD_STREAM_RAW;

    faad_status st = faad_decoder_open(&cfg, asc, len, &dec);
    uint32_t rate = 0;
    if (st == FAAD_OK) {
        faad_stream_info info;
        info.struct_size = sizeof(info);
        if (faad_decoder_get_info(dec, &info) == FAAD_OK)
            rate = info.sample_rate;
        faad_decoder_close(&dec);
    }
    if (st != want || rate != want_rate) {
        printf("FAIL %s: status %d rate %u (want %d, %u)\n", name, (int)st, (unsigned)rate,
               (int)want, (unsigned)want_rate);
        failures++;
    }
}

int main(void)
{
    /* LC 44.1 kHz stereo, dependsOnCoreCoder set but no coreCoderDelay (Audible). */
    static const uint8_t no_delay[] = { 0x12, 0x12 };
    /* The same with the 14-bit delay and extensionFlag present. */
    static const uint8_t with_delay[] = { 0x12, 0x12, 0x00, 0x00 };
    /* With the delay, followed by the explicit SBR extension (88.2 kHz). */
    static const uint8_t with_delay_sbr[] = { 0x12, 0x12, 0x00, 0x01, 0x5b, 0x96, 0x20 };
    /* Plain LC without dependsOnCoreCoder. */
    static const uint8_t plain[] = { 0x12, 0x10 };

    check("no coreCoderDelay", no_delay, sizeof(no_delay), FAAD_OK, 44100);
    check("coreCoderDelay present", with_delay, sizeof(with_delay), FAAD_OK, 44100);
    check("coreCoderDelay then SBR extension", with_delay_sbr, sizeof(with_delay_sbr), FAAD_OK, 88200);
    check("plain LC", plain, sizeof(plain), FAAD_OK, 44100);
    return failures ? 1 : 0;
}
