#ifndef FAAR_BACKEND_DISPATCH_H
#define FAAR_BACKEND_DISPATCH_H

#include "faar.h"

typedef struct faar_backend_ops {
    faar_status (*open)(const faar_config *, void **);
    faar_status (*prepare)(void *, size_t);
    faar_status (*close)(void **);
    faar_status (*get_info)(const void *, faar_resampler_info *);
    faar_status (*process)(void *, const float *, size_t, float *, size_t, size_t *);
    faar_status (*flush)(void *, float *, size_t, size_t *);
} faar_backend_ops;

extern const faar_backend_ops faar_float_backend_ops;
extern const faar_backend_ops faar_double_backend_ops;

#endif
