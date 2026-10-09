/*
 * FAAM - Freeware Advanced Audio Muxer
 * Copyright (C) 2026 Nils Schimmelmann
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */

/*
 * Utility functions and error strings for libfaam
 */

#include "libfaam_internal.h"

FAAMAPI faam_status faam_get_library_info(faam_library_info *out)
{
    if (!out || out->struct_size < FAAM_LIBRARY_INFO_BASELINE)
        return FAAM_ERR_INVALID_ARG;

    faam_library_info info;
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.max_tracks = FAAM_MAX_TRACKS;
#ifdef FAAM_MUXER_VIDEO
    info.features |= FAAM_FEATURE_VIDEO;
#endif
#ifdef FAAM_MUXER_FRAGMENTED
    info.features |= FAAM_FEATURE_FRAGMENTED;
#endif
    info.version = FAAM_VERSION_STRING;
    info.copyright = "Copyright (C) 2026 Nils Schimmelmann";
    faam_copy_out(out, out->struct_size, &info, sizeof(info));
    return FAAM_OK;
}

int32_t faam_io_read_full(const faam_io *io, void *buf, uint32_t n)
{
    uint8_t *p = (uint8_t *)buf;
    uint32_t done = 0;
    while (done < n) {
        int32_t r = io->read(io->user_data, p + done, n - done);
        if (r < 0 || (uint32_t)r > n - done) return -1;
        if (r == 0) break;
        done += (uint32_t)r;
    }
    return (int32_t)done;
}

const char *faam_strerror(faam_status status)
{
    switch (status) {
    case FAAM_OK:
        return "Success";
    case FAAM_ERR_INVALID_ARG:
        return "Invalid argument or null pointer";
    case FAAM_ERR_BAD_CONTAINER:
        return "Invalid MP4 atom structure or corrupt container";
    case FAAM_ERR_IO_READ:
        return "I/O read error or premature EOF";
    case FAAM_ERR_IO_WRITE:
        return "I/O write error";
    case FAAM_ERR_INSUFFICIENT_MEM:
        return "Insufficient memory";
    case FAAM_ERR_NO_TRACK:
        return "No matching video/audio track found in container";
    case FAAM_ERR_OUTPUT_TOO_SMALL:
        return "Output buffer too small";
    case FAAM_END_OF_STREAM:
        return "End of stream";
    case FAAM_ERR_UNSUPPORTED:
        return "Operation or content not supported";
    case FAAM_ERR_NOT_BUILT:
        return "Feature compiled out of this build";
    default:
        return "Unknown error code";
    }
}
