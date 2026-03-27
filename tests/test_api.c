#include <faac.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define SAMPLE_RATE 44100
#define CHANNELS 2

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int main() {
    faacEncHandle hEncoder;
    faacEncConfigurationPtr config;
    unsigned long inputSamples, maxOutputBytes;
    int32_t *inputBuffer;
    unsigned char *outputBuffer;
    int i, frame;

    hEncoder = faacEncOpen(SAMPLE_RATE, CHANNELS, &inputSamples, &maxOutputBytes);
    if (!hEncoder) {
        fprintf(stderr, "faacEncOpen failed\n");
        return 1;
    }

    config = faacEncGetCurrentConfiguration(hEncoder);
    config->bitRate = 128000 / CHANNELS;
    config->inputFormat = FAAC_INPUT_32BIT;
    config->outputFormat = 1; // ADTS
    config->useTns = 1;
    config->aacObjectType = LOW;
    config->jointmode = JOINT_MS;

    if (!faacEncSetConfiguration(hEncoder, config)) {
        fprintf(stderr, "faacEncSetConfiguration failed\n");
        faacEncClose(hEncoder);
        return 1;
    }

    inputBuffer = calloc(inputSamples, sizeof(int32_t));
    outputBuffer = malloc(maxOutputBytes);

    // Test with different signal types
    for (frame = 0; frame < 10; frame++) {
        // Generate a simple signal
        for (i = 0; i < inputSamples; i++) {
            if (frame < 3) { // Silence
                inputBuffer[i] = 0;
            } else if (frame < 6) { // Full scale noise
                inputBuffer[i] = (rand() % 65536 - 32768) << 16;
            } else { // Sine wave
                inputBuffer[i] = (int32_t)(sin(2.0 * M_PI * 1000.0 * (double)(i + frame * inputSamples) / SAMPLE_RATE) * 2147483647.0);
            }
        }

        int bytesWritten = faacEncEncode(hEncoder, inputBuffer, inputSamples, outputBuffer, maxOutputBytes);
        if (bytesWritten < 0) {
            fprintf(stderr, "faacEncEncode failed at frame %d\n", frame);
            free(inputBuffer);
            free(outputBuffer);
            faacEncClose(hEncoder);
            return 1;
        }
        printf("Frame %d: %d bytes written\n", frame, bytesWritten);
    }

    // Flush
    while (1) {
        int bytesWritten = faacEncEncode(hEncoder, NULL, 0, outputBuffer, maxOutputBytes);
        if (bytesWritten <= 0) break;
        printf("Flush: %d bytes written\n", bytesWritten);
    }

    faacEncClose(hEncoder);
    free(inputBuffer);
    free(outputBuffer);

    printf("API sanity test passed\n");
    return 0;
}
