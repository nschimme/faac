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

/* Includes only the public header and calls one function, to prove a consumer
 * links against a library flavour without defining anything. */
#if defined(CHECK_FAAC)
#include <faac.h>
int main(void)
{
    faac_params p;
    return faac_params_init(&p, sizeof p) == FAAC_OK ? 0 : 1;
}
#elif defined(CHECK_FAAD)
#include <faad3.h>
int main(void)
{
    faad_config c;
    return faad_config_init(&c, sizeof c) == FAAD_OK ? 0 : 1;
}
#elif defined(CHECK_FAAR)
#include <faar.h>
int main(void)
{
    faar_config c;
    return faar_config_init(&c, sizeof c) == FAAR_OK ? 0 : 1;
}
#else
#include <faam.h>
int main(void)
{
    faam_muxer_config c;
    return faam_muxer_config_init(&c, sizeof c) == FAAM_OK ? 0 : 1;
}
#endif
