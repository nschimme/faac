#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <faac.h>
#include <assert.h>

void test_api_config(unsigned long samplerate, unsigned int channels) {
    faacEncHandle h;
    unsigned long inputSamples, maxOutputBytes;
    faacEncConfigurationPtr config;

    printf("Testing API config: %lu Hz, %u ch\n", samplerate, channels);
    h = faacEncOpen(samplerate, channels, &inputSamples, &maxOutputBytes);
    assert(h != NULL);

    config = faacEncGetCurrentConfiguration(h);
    assert(config != NULL);

    config->bitRate = 128000 / channels;
    config->inputFormat = FAAC_INPUT_FLOAT;
    config->outputFormat = 1; // ADTS

    int ret = faacEncSetConfiguration(h, config);
    assert(ret == 1);

    faacEncClose(h);
}

void test_encode_silence(unsigned long samplerate, unsigned int channels) {
    faacEncHandle h;
    unsigned long inputSamples, maxOutputBytes;
    faacEncConfigurationPtr config;

    printf("Testing encode silence: %lu Hz, %u ch\n", samplerate, channels);
    h = faacEncOpen(samplerate, channels, &inputSamples, &maxOutputBytes);
    assert(h != NULL);

    config = faacEncGetCurrentConfiguration(h);
    config->bitRate = 64000;
    config->inputFormat = FAAC_INPUT_FLOAT;
    faacEncSetConfiguration(h, config);

    float *input = calloc(inputSamples, sizeof(float));
    unsigned char *output = malloc(maxOutputBytes);

    // Encode a few frames of silence
    for (int i = 0; i < 10; i++) {
        int bytes = faacEncEncode(h, (int32_t*)input, inputSamples, output, maxOutputBytes);
        assert(bytes >= 0);
    }

    // Flush
    int bytes;
    do {
        bytes = faacEncEncode(h, NULL, 0, output, maxOutputBytes);
        assert(bytes >= 0);
    } while (bytes > 0);

    free(input);
    free(output);
    faacEncClose(h);
}

void test_encode_noise(unsigned long samplerate, unsigned int channels) {
    faacEncHandle h;
    unsigned long inputSamples, maxOutputBytes;
    faacEncConfigurationPtr config;

    printf("Testing encode noise: %lu Hz, %u ch\n", samplerate, channels);
    h = faacEncOpen(samplerate, channels, &inputSamples, &maxOutputBytes);
    assert(h != NULL);

    config = faacEncGetCurrentConfiguration(h);
    config->bitRate = 128000 / channels;
    config->inputFormat = FAAC_INPUT_FLOAT;
    faacEncSetConfiguration(h, config);

    float *input = malloc(inputSamples * sizeof(float));
    unsigned char *output = malloc(maxOutputBytes);

    for (unsigned long i = 0; i < inputSamples; i++) {
        input[i] = (float)((rand() % 20000) - 10000);
    }

    for (int i = 0; i < 5; i++) {
        int bytes = faacEncEncode(h, (int32_t*)input, inputSamples, output, maxOutputBytes);
        assert(bytes >= 0);
    }

    free(input);
    free(output);
    faacEncClose(h);
}

int main() {
    unsigned long rates[] = {8000, 16000, 44100, 48000};
    unsigned int channels[] = {1, 2, 6};

    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 3; c++) {
            test_api_config(rates[r], channels[c]);
            test_encode_silence(rates[r], channels[c]);
            test_encode_noise(rates[r], channels[c]);
        }
    }

    printf("All API tests passed!\n");
    return 0;
}
