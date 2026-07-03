#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <emscripten.h>

#include <faac.h>
#include "mp4write.h"
#include "utils.h"

typedef struct {
    faacEncHandle hEncoder;
    unsigned long samplesInput;
    unsigned long maxBytesOutput;
    unsigned char *bitbuf;
    int is_mp4;
    uint32_t samplerate;
    uint32_t channels;
    FILE *aac_out;
} faac_wasm_t;

EMSCRIPTEN_KEEPALIVE
faac_wasm_t *faac_wasm_init(int samplerate, int channels, int bitrate, int quality, int use_mp4) {
    faac_wasm_t *ctx = malloc(sizeof(faac_wasm_t));
    if (!ctx) return NULL;
    memset(ctx, 0, sizeof(faac_wasm_t));

    ctx->hEncoder = faacEncOpen(samplerate, channels, &ctx->samplesInput, &ctx->maxBytesOutput);
    if (!ctx->hEncoder) {
        free(ctx);
        return NULL;
    }

    faacEncConfigurationPtr config = faacEncGetCurrentConfiguration(ctx->hEncoder);
    config->inputFormat = FAAC_INPUT_FLOAT;
    config->outputFormat = use_mp4 ? RAW_STREAM : ADTS_STREAM;
    config->mpegVersion = MPEG4;
    config->aacObjectType = LOW;
    config->useTns = 1;
    config->jointmode = JOINT_MIXED;

    if (quality > 0) {
        config->quantqual = quality;
        config->bitRate = 0;
    } else if (bitrate > 0) {
        config->bitRate = (bitrate * 1000) / channels;
    }

    if (!faacEncSetConfiguration(ctx->hEncoder, config)) {
        faacEncClose(ctx->hEncoder);
        free(ctx);
        return NULL;
    }

    ctx->bitbuf = malloc(ctx->maxBytesOutput);
    ctx->is_mp4 = use_mp4;
    ctx->samplerate = samplerate;
    ctx->channels = channels;

    if (use_mp4) {
        mp4_open("output.bin", 1);
        mp4_set_format(samplerate, channels, 16);
    } else {
        ctx->aac_out = fopen("output.bin", "wb");
    }

    return ctx;
}

EMSCRIPTEN_KEEPALIVE
int faac_wasm_encode(faac_wasm_t *ctx, float *pcm_data, int samples_read) {
    int bytesWritten = faacEncEncode(ctx->hEncoder, (int32_t *)pcm_data, samples_read, ctx->bitbuf, ctx->maxBytesOutput);

    if (bytesWritten > 0) {
        if (ctx->is_mp4) {
            mp4_write_frame(ctx->bitbuf, bytesWritten, 1024);
        } else if (ctx->aac_out) {
            fwrite(ctx->bitbuf, 1, bytesWritten, ctx->aac_out);
        }
    }

    return bytesWritten;
}

EMSCRIPTEN_KEEPALIVE
void faac_wasm_close(faac_wasm_t *ctx) {
    if (!ctx) return;

    // Flush encoder
    int bytesWritten;
    do {
        bytesWritten = faacEncEncode(ctx->hEncoder, NULL, 0, ctx->bitbuf, ctx->maxBytesOutput);
        if (bytesWritten > 0) {
            if (ctx->is_mp4) {
                mp4_write_frame(ctx->bitbuf, bytesWritten, 1024);
            } else if (ctx->aac_out) {
                fwrite(ctx->bitbuf, 1, bytesWritten, ctx->aac_out);
            }
        }
    } while (bytesWritten > 0);

    if (ctx->is_mp4) {
        char *id_string, *copyright_string;
        faac_check_version(&id_string, &copyright_string);
        faac_mp4_finish(ctx->hEncoder, id_string);
    } else if (ctx->aac_out) {
        fclose(ctx->aac_out);
    }

    faacEncClose(ctx->hEncoder);
    free(ctx->bitbuf);
    free(ctx);
}
