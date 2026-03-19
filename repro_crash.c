#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <faac.h>

int main() {
    unsigned long sampleRate = 48000;
    unsigned int numChn = 2;
    unsigned long inputSamples;
    unsigned long outputBufferSize;

    faacEncHandle handle = faacEncOpen(sampleRate, numChn, &inputSamples, &outputBufferSize);
    if (!handle) {
        fprintf(stderr, "Failed to open FAAC encoder\n");
        return 1;
    }

    faacEncConfigurationPtr config = faacEncGetCurrentConfiguration(handle);
    config->bitRate = 128000;
    config->inputFormat = FAAC_INPUT_16BIT;
    config->outputFormat = 0; // RAW_STREAM
    config->quantqual = 0; // Trigger quality = 0? Wait, quantqual 0 is default DEFQUAL

    if (!faacEncSetConfiguration(handle, config)) {
        fprintf(stderr, "Failed to configure FAAC encoder\n");
        return 1;
    }

    // We want to force quality to 0 in faacEncEncode
    // It's adjusted based on frame size.

    int16_t *inputBuffer = (int16_t *)calloc(inputSamples, sizeof(int16_t));
    unsigned char *outputBuffer = (unsigned char *)malloc(outputBufferSize);

    for (int i = 0; i < 20; i++) {
        faacEncEncode(handle, (int32_t *)inputBuffer, inputSamples, outputBuffer, outputBufferSize);
    }

    free(inputBuffer);
    free(outputBuffer);
    faacEncClose(handle);

    return 0;
}
