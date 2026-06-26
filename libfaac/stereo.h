/****************************************************************************
    Intensity Stereo

    Copyright (C) 2017 Krzysztof Nikiel

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
****************************************************************************/

#ifndef STEREO_H
#define STEREO_H

#include "channels.h"
#include "util.h"

typedef struct {
    faac_real coll_thr_lo;
    faac_real coll_thr_mid;
    faac_real coll_thr_hi;
    faac_real coll_thr_scale;
    faac_real is_freq_lo;
    faac_real is_freq_hi;
    faac_real thrside_scale;
    faac_real ms_side;
    faac_real ms_side_lo_sfb;
} StereoTuning;

void AACstereo(CoderInfo *coder,
               ChannelInfo *channel,
               faac_real *s[MAX_CHANNELS],
               int maxchan,
               faac_real quality,
               int mode,
               int sampleRate,
               const StereoTuning *tune
              );

#endif
