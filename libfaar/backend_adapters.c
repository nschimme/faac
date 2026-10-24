#include "backend_rename.h"
#include "backend_dispatch.h"

#include <stddef.h>

#if defined(FAAR_BACKEND_FLOAT)
# define BACKEND_ADAPTER(name) faar_float_backend_##name
# define BACKEND_OPS faar_float_backend_ops
#elif defined(FAAR_BACKEND_DOUBLE)
# define BACKEND_ADAPTER(name) faar_double_backend_##name
# define BACKEND_OPS faar_double_backend_ops
#else
# error "Select one FAAR precision backend"
#endif

static faar_status BACKEND_ADAPTER(open)(const faar_config *config, void **state) {
    if (!state) return FAAR_ERR_INVALID_ARGUMENT;
    *state = NULL;
    faar_resampler *handle = NULL;
    faar_status status = faar_resampler_open(config, &handle);
    *state = handle;
    return status;
}

static faar_status BACKEND_ADAPTER(prepare)(void *state, size_t max_input) {
    return faar_resampler_prepare((faar_resampler *)state, max_input);
}

static faar_status BACKEND_ADAPTER(close)(void **state) {
    if (!state) return FAAR_ERR_INVALID_ARGUMENT;
    faar_resampler *handle = (faar_resampler *)*state;
    faar_status status = faar_resampler_close(&handle);
    *state = handle;
    return status;
}

static faar_status BACKEND_ADAPTER(get_info)(const void *state,
        faar_resampler_info *out) {
    return faar_resampler_get_info((const faar_resampler *)state, out);
}

static faar_status BACKEND_ADAPTER(process)(void *state, const float *input,
        size_t input_frames, float *output, size_t output_capacity,
        size_t *output_frames) {
    return faar_resampler_process((faar_resampler *)state, input, input_frames,
            output, output_capacity, output_frames);
}

static faar_status BACKEND_ADAPTER(flush)(void *state, float *output,
        size_t output_capacity, size_t *output_frames) {
    return faar_resampler_flush((faar_resampler *)state, output,
            output_capacity, output_frames);
}

const faar_backend_ops BACKEND_OPS = {
    BACKEND_ADAPTER(open),
    BACKEND_ADAPTER(prepare),
    BACKEND_ADAPTER(close),
    BACKEND_ADAPTER(get_info),
    BACKEND_ADAPTER(process),
    BACKEND_ADAPTER(flush),
};
