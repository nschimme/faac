/*
 * Utility functions, common file I/O callbacks, and error strings for libfaam
 */

#include "libfaam_internal.h"

int32_t faam_file_read_cb(void *user_data, void *buf, uint32_t bytes) {
    return (int32_t)fread(buf, 1, bytes, (FILE *)user_data);
}

int32_t faam_file_write_cb(void *user_data, const void *buf, uint32_t bytes) {
    return (int32_t)fwrite(buf, 1, bytes, (FILE *)user_data);
}

bool faam_file_seek_cb(void *user_data, uint64_t offset) {
    return fseek((FILE *)user_data, (long)offset, SEEK_SET) == 0;
}

uint64_t faam_file_tell_cb(void *user_data) {
    return (uint64_t)ftell((FILE *)user_data);
}

faam_status faam_get_library_info(faam_library_info *out)
{
    if (!out || out->struct_size < sizeof(faam_library_info)) {
        return FAAM_ERR_INVALID_ARG;
    }

    out->struct_size = sizeof(faam_library_info);
    out->version = "1.0.0";
    out->copyright = "Copyright (C) 2026 FAAC Project";
    out->max_channels = MAX_CHANNELS;

    return FAAM_OK;
}

const char *faam_strerror(faam_status status)
{
    switch (status) {
    case FAAM_OK:
        return "Success";
    case FAAM_ERR_INVALID_ARG:
        return "Invalid argument or null pointer";
    case FAAM_ERR_BAD_CONTAINER:
        return "Invalid MP4/M4A atom structure or corrupt container";
    case FAAM_ERR_IO_READ:
        return "I/O read error or premature EOF";
    case FAAM_ERR_IO_WRITE:
        return "I/O write error";
    case FAAM_ERR_INSUFFICIENT_MEM:
        return "Insufficient static memory arena or allocation buffer";
    case FAAM_ERR_NO_AUDIO_TRACK:
        return "No AAC audio track found in container";
    default:
        return "Unknown error code";
    }
}
