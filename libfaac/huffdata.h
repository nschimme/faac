/****************************************************************************
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

#ifndef HUFFDATA_H
#define HUFFDATA_H

#include "huff2.h"
#include <stdint.h>

extern const uint8_t huff_len[];
extern const uint16_t huff_data[];
extern const int huff_offset[];

extern const uint8_t huff_len_delta[];
extern const uint32_t huff_data_delta[];

#endif /* HUFFDATA_H */
