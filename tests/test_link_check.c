/*
 * FAAC - Freeware Advanced Audio Coder
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

/* Includes only the public header and takes the address of every exported
 * function, to prove a consumer links against a library flavour without defining
 * anything and that the symbol map and .def keep up with the header. */

#if defined(CHECK_FAAC)
#include <faac.h>
int main(void)
{
    static void (*const volatile fn[])(void) = {
        (void (*)(void))faac_get_library_info,
        (void (*)(void))faac_params_init,
        (void (*)(void))faac_encoder_open,
        (void (*)(void))faac_encoder_close,
        (void (*)(void))faac_encoder_get_info,
        (void (*)(void))faac_encoder_asc,
        (void (*)(void))faac_encoder_encode,
        (void (*)(void))faac_strerror
    };
    for (unsigned i = 0; i < sizeof fn / sizeof fn[0]; i++)
        if (!fn[i])
            return 1;
    return 0;
}
#elif defined(CHECK_FAAD)
#include <faad3.h>
int main(void)
{
    static void (*const volatile fn[])(void) = {
        (void (*)(void))faad_get_library_info,
        (void (*)(void))faad_config_init,
        (void (*)(void))faad_decoder_open,
        (void (*)(void))faad_decoder_close,
        (void (*)(void))faad_decoder_get_info,
        (void (*)(void))faad_decoder_flush,
        (void (*)(void))faad_decode_frame,
        (void (*)(void))faad_strerror
    };
    for (unsigned i = 0; i < sizeof fn / sizeof fn[0]; i++)
        if (!fn[i])
            return 1;
    return 0;
}
#elif defined(CHECK_FAAR)
#include <faar.h>
int main(void)
{
    static void (*const volatile fn[])(void) = {
        (void (*)(void))faar_config_init,
        (void (*)(void))faar_get_library_info,
        (void (*)(void))faar_strerror,
        (void (*)(void))faar_resampler_open,
        (void (*)(void))faar_design_open,
        (void (*)(void))faar_design_close,
        (void (*)(void))faar_resampler_open_from_design,
        (void (*)(void))faar_resampler_close,
        (void (*)(void))faar_resampler_get_info,
        (void (*)(void))faar_resampler_process,
        (void (*)(void))faar_resampler_finalize
    };
    for (unsigned i = 0; i < sizeof fn / sizeof fn[0]; i++)
        if (!fn[i])
            return 1;
    return 0;
}
#else
#include <faam.h>
int main(void)
{
    static void (*const volatile fn[])(void) = {
        (void (*)(void))faam_get_library_info,
        (void (*)(void))faam_demuxer_config_init,
        (void (*)(void))faam_io_init,
        (void (*)(void))faam_track_config_init,
        (void (*)(void))faam_demuxer_open,
        (void (*)(void))faam_demuxer_close,
        (void (*)(void))faam_demuxer_get_num_tracks,
        (void (*)(void))faam_demuxer_get_track_info,
        (void (*)(void))faam_demuxer_get_codec_data,
        (void (*)(void))faam_demuxer_get_major_brand,
        (void (*)(void))faam_demuxer_get_track_gapless,
        (void (*)(void))faam_demuxer_get_num_metadata,
        (void (*)(void))faam_demuxer_get_metadata_entry,
        (void (*)(void))faam_demuxer_get_num_chapters,
        (void (*)(void))faam_demuxer_get_chapter,
        (void (*)(void))faam_demuxer_next_frame_loc,
        (void (*)(void))faam_demuxer_read_frame,
        (void (*)(void))faam_muxer_config_init,
        (void (*)(void))faam_muxer_open,
        (void (*)(void))faam_muxer_get_track_id,
        (void (*)(void))faam_muxer_update,
        (void (*)(void))faam_muxer_write_frame,
        (void (*)(void))faam_muxer_finalize,
        (void (*)(void))faam_muxer_close,
        (void (*)(void))faam_muxer_get_info,
        (void (*)(void))faam_strerror,
        (void (*)(void))faam_update_metadata_stream,
        (void (*)(void))faam_update_chapters_stream
    };
    for (unsigned i = 0; i < sizeof fn / sizeof fn[0]; i++)
        if (!fn[i])
            return 1;
    return 0;
}
#endif
