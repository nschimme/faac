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

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "libfaac/coder.h"
#include "../libfaac/huff2.c"

void test_escape() {
    int code;
    int len;

    /* Huffman Escape coding boundaries (limit 8192) */
    len = escape(16, &code);
    assert(len == 5);
    assert(code == 0);

    len = escape(31, &code);
    assert(len == 5);
    assert(code == 15);

    len = escape(32, &code);
    assert(len == 7);
    assert(code == 64);

    len = escape(8191, &code);
    assert(len > 0);
}

void test_huffbook() {
    CoderInfo coder;
    int qs[1024];
    memset(&coder, 0, sizeof(coder));
    coder.bandcnt = 0;

    /* Optimal book selection based on spectral maximums */
    for(int i=0; i<1024; i++) qs[i] = 0;
    huffbook(&coder, qs, 4);
    assert(coder.book[0] == HCB_ZERO);

    coder.bandcnt = 1;
    qs[0] = 1;
    huffbook(&coder, qs, 4);
    assert(coder.book[1] == 1 || coder.book[1] == 2);

    coder.bandcnt = 2;
    qs[0] = 20;
    huffbook(&coder, qs, 4);
    assert(coder.book[2] == HCB_ESC);
}

int main() {
    test_escape();
    test_huffbook();
    printf("test_huffman passed\n");
    return 0;
}
