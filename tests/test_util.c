/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2001 Menno Bakker
 * Copyright (C) 2002-2026 Krzysztof Nikiel
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

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <assert.h>
#include <stdio.h>
#include "util.h"

void test_get_sr_index() {
    printf("Testing GetSRIndex...\n");
    // Nominal rates
    assert(GetSRIndex(96000) == 0);
    assert(GetSRIndex(88200) == 1);
    assert(GetSRIndex(64000) == 2);
    assert(GetSRIndex(48000) == 3);
    assert(GetSRIndex(44100) == 4);
    assert(GetSRIndex(32000) == 5);
    assert(GetSRIndex(24000) == 6);
    assert(GetSRIndex(22050) == 7);
    assert(GetSRIndex(16000) == 8);
    assert(GetSRIndex(12000) == 9);
    assert(GetSRIndex(11025) == 10);
    assert(GetSRIndex(8000) == 11);

    // Edge cases and boundaries
    assert(GetSRIndex(100000) == 0);
    assert(GetSRIndex(0) == 11);
    assert(GetSRIndex(44200) == 4); // Between 44100 and 48000
    assert(GetSRIndex(46009) == 3); // Exact boundary
}

int main() {
    test_get_sr_index();
    printf("All util tests passed!\n");
    return 0;
}
