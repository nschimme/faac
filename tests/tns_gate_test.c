#include <stdio.h>
#include <stdlib.h>
#include <faac.h>

typedef struct {
    unsigned long sampleRate;
    unsigned int numChannels;
    unsigned long bitrate;
    unsigned long quality;
    int expectedTns;
} TestConfig;

int run_test(TestConfig cfg) {
    faacEncHandle hEncoder;
    unsigned long inputSamples, maxOutputBytes;
    faacEncConfigurationPtr config;

    hEncoder = faacEncOpen(cfg.sampleRate, cfg.numChannels, &inputSamples, &maxOutputBytes);
    if (!hEncoder) {
        printf("Failed to open encoder\n");
        return 1;
    }

    config = faacEncGetCurrentConfiguration(hEncoder);
    config->bitRate = cfg.bitrate / cfg.numChannels;
    config->quantqual = cfg.quality;
    config->useTns = 1;

    if (!faacEncSetConfiguration(hEncoder, config)) {
        printf("Failed to set configuration\n");
        faacEncClose(hEncoder);
        return 1;
    }

    /* Check resolved TNS state */
    int resolvedTns = config->useTns;
    faacEncClose(hEncoder);

    if (resolvedTns != cfg.expectedTns) {
        printf("FAIL: SR=%lu, Ch=%u, BR=%lu, Q=%lu | Expected TNS=%d, Got=%d\n",
               cfg.sampleRate, cfg.numChannels, cfg.bitrate, cfg.quality, cfg.expectedTns, resolvedTns);
        return 1;
    }

    printf("PASS: SR=%lu, Ch=%u, BR=%lu, Q=%lu | TNS=%d\n",
           cfg.sampleRate, cfg.numChannels, cfg.bitrate, cfg.quality, resolvedTns);
    return 0;
}

int main() {
    TestConfig tests[] = {
        /* CBR/ABR tests */
        {44100, 2, 64000, 0, 1},  /* 32kbps/ch -> TNS ON */
        {44100, 2, 128000, 0, 0}, /* 64kbps/ch -> TNS OFF (Gated) */
        {44100, 1, 32000, 0, 1},  /* 32kbps/ch mono -> TNS ON */
        {44100, 1, 64000, 0, 0},  /* 64kbps/ch mono -> TNS OFF (Gated) */
        {48000, 6, 192000, 0, 1}, /* 32kbps/ch 5.1 -> TNS ON */
        {48000, 6, 384000, 0, 0}, /* 64kbps/ch 5.1 -> TNS OFF (Gated) */

        /* VBR tests (Project anchor: Q100 approx 64kbps/ch for stereo 44.1kHz) */
        /* effectiveBitratePerCh = (quality * 1280) / hEncoder->numChannels */
        {44100, 2, 0, 50, 1},   /* effBR = (50 * 1280) / 2 = 32000 -> TNS ON */
        {44100, 2, 0, 100, 0},  /* effBR = (100 * 1280) / 2 = 64000 -> TNS OFF (Gated) */
        {44100, 1, 0, 25, 1},   /* effBR = (25 * 1280) / 1 = 32000 -> TNS ON */
        {44100, 1, 0, 50, 0},   /* effBR = (50 * 1280) / 1 = 64000 -> TNS OFF (Gated) */
        {48000, 6, 0, 150, 1},  /* effBR = (150 * 1280) / 6 = 32000 -> TNS ON */
        {48000, 6, 0, 300, 0},  /* effBR = (300 * 1280) / 6 = 64000 -> TNS OFF (Gated) */
    };

    int i, failed = 0;
    int numTests = sizeof(tests) / sizeof(tests[0]);

    for (i = 0; i < numTests; i++) {
        if (run_test(tests[i])) {
            failed++;
        }
    }

    if (failed) {
        printf("\nTotal failed tests: %d\n", failed);
        return 1;
    }

    printf("\nAll TNS gating tests passed!\n");
    return 0;
}
