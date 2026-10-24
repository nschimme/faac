#include "backend_dispatch.h"

#include <stdlib.h>
#include <string.h>

#ifndef MAX_CHANNELS
# define MAX_CHANNELS 8
#endif

struct faar_resampler_struct {
    const faar_backend_ops *ops;
    void *state;
};

faar_status faar_config_init(faar_config *config, uint32_t caller_size) {
    faar_config defaults;
    if (!config || caller_size < FAAR_CONFIG_BASELINE)
        return FAAR_ERR_INVALID_ARGUMENT;
    memset(&defaults, 0, sizeof(defaults));
    defaults.quality = FAAR_QUALITY_GOOD;
    defaults.boundary = FAAR_BOUNDARY_HOLD;
    defaults.dither = FAAR_DITHER_OFF;
    uint32_t n = caller_size < sizeof(defaults) ? caller_size : sizeof(defaults);
    defaults.struct_size = n;
    memcpy(config, &defaults, n);
    return FAAR_OK;
}

faar_status faar_get_library_info(faar_library_info *out) {
    if (!out || out->struct_size < FAAR_LIBRARY_INFO_BASELINE)
        return FAAR_ERR_INVALID_ARGUMENT;
    uint32_t caller_size = out->struct_size;
    faar_library_info info = {0};
    info.max_channels = MAX_CHANNELS;
    info.quality_mask = FAAR_QUALITY_MASK_FAST | FAAR_QUALITY_MASK_GOOD
        | FAAR_QUALITY_MASK_HIGH;
    info.version = FAAR_VERSION_STRING;
    info.copyright =
        "FAAR - Freeware Advanced Audio Resampler (https://freewareadvancedaudio.github.io)\n"
        " Copyright (C) 2026, Nils Schimmelmann";
    uint32_t n = caller_size < sizeof(info) ? caller_size : sizeof(info);
    info.struct_size = n;
    memcpy(out, &info, n);
    return FAAR_OK;
}

const char *faar_strerror(faar_status status) {
    switch (status) {
        case FAAR_OK: return "success";
        case FAAR_ERR_INVALID_ARGUMENT: return "invalid argument or state";
        case FAAR_ERR_UNSUPPORTED: return "unsupported configuration";
        case FAAR_ERR_NO_MEMORY: return "out of memory";
        case FAAR_ERR_PROCESSING: return "resampling failed; discard this stream";
        default: return "unknown FAAR status";
    }
}

faar_status faar_resampler_open(const faar_config *config, faar_resampler **out) {
    if (!out) return FAAR_ERR_INVALID_ARGUMENT;
    *out = NULL;

    /* Let the selected backend perform the full versioned-configuration
     * validation. Avoid reading quality until that field is present. */
    bool high = config && config->struct_size >= FAAR_STRUCT_END(faar_config, quality)
        && config->quality == FAAR_QUALITY_HIGH;
    const faar_backend_ops *ops = high
        ? &faar_double_backend_ops : &faar_float_backend_ops;
    struct faar_resampler_struct *handle = calloc(1, sizeof(*handle));
    if (!handle) return FAAR_ERR_NO_MEMORY;
    faar_status status = ops->open(config, &handle->state);
    if (status != FAAR_OK) {
        free(handle);
        return status;
    }
    handle->ops = ops;
    *out = handle;
    return FAAR_OK;
}

faar_status faar_resampler_prepare(faar_resampler *resampler,
        size_t max_input_frames) {
    if (!resampler) return FAAR_ERR_INVALID_ARGUMENT;
    return resampler->ops->prepare(resampler->state, max_input_frames);
}

faar_status faar_resampler_close(faar_resampler **resampler) {
    if (!resampler) return FAAR_ERR_INVALID_ARGUMENT;
    if (!*resampler) return FAAR_OK;
    struct faar_resampler_struct *handle = *resampler;
    faar_status status = handle->ops->close(&handle->state);
    if (status == FAAR_OK) {
        free(handle);
        *resampler = NULL;
    }
    return status;
}

faar_status faar_resampler_get_info(const faar_resampler *resampler,
        faar_resampler_info *out) {
    if (!resampler) return FAAR_ERR_INVALID_ARGUMENT;
    return resampler->ops->get_info(resampler->state, out);
}

faar_status faar_resampler_process(faar_resampler *resampler,
        const float *input, size_t input_frames, float *output,
        size_t output_capacity, size_t *output_frames) {
    if (!resampler) {
        if (output_frames) *output_frames = 0;
        return FAAR_ERR_INVALID_ARGUMENT;
    }
    return resampler->ops->process(resampler->state, input, input_frames,
            output, output_capacity, output_frames);
}

faar_status faar_resampler_flush(faar_resampler *resampler, float *output,
        size_t output_capacity, size_t *output_frames) {
    if (!resampler) {
        if (output_frames) *output_frames = 0;
        return FAAR_ERR_INVALID_ARGUMENT;
    }
    return resampler->ops->flush(resampler->state, output, output_capacity,
            output_frames);
}
