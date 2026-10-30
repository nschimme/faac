/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2026 Nils Schimmelmann
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Encoder quirks shared by the frontends: encoders that prepend a fixed
 * number of priming samples but may have no gapless container metadata.
 * Priming is encoder-specific, so only known families get an inferred value.
 */

#ifndef FAAC_FRONTEND_ENCODER_QUIRKS_H
#define FAAC_FRONTEND_ENCODER_QUIRKS_H

#include <stdbool.h>
#include <stdint.h>

#include "faam.h"

/* Looks up the priming samples for an Encoder/Tool tag value. Returns false
 * when the encoder is unknown, leaving *priming untouched. */
bool encoder_quirk_priming(const char *encoder_tag, uint32_t *priming);

/* Same as encoder_quirk_priming() for a non-NUL-terminated metadata value. */
bool encoder_quirk_priming_bytes(const uint8_t *data, uint32_t len, uint32_t *priming);

/* Resolves a track's encoder priming the way both faad and faam must see it:
 * explicit container gapless metadata wins, and a zero delay falls back to the
 * quirk table using the file's Encoder/Tool tag. Only AAC tracks qualify, so
 * an "Apple" tag on, say, an ALAC track invents nothing. track_id 0 means the
 * first audio track. Sets *inferred when the tag supplied the value. Pass NULL
 * for *inferred when only the value is needed. */
uint32_t encoder_quirk_resolve_delay(const faam_demuxer *demuxer, uint32_t track_id,
                                     uint32_t explicit_delay, bool *inferred);

#endif
