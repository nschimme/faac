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

    // x = 16: base 16. code = 0. len = 5.
    len = escape(16, &code);
    assert(len == 5);
    assert(code == 0);

    // x = 31: base 16. code = 0.
    len = escape(31, &code);
    assert(len == 5);
    assert(code == 15);

    // x = 32: base 32 <= 32. base becomes 64. preflen = 1.
    len = escape(32, &code);
    assert(len == 7);
    assert(code == 64);

    // Boundary check for escape: limit is 8192
    len = escape(8191, &code);
    assert(len > 0);
}

void test_huffbook() {
    CoderInfo coder;
    int qs[1024];
    memset(&coder, 0, sizeof(coder));
    coder.bandcnt = 0;

    // All zeros
    for(int i=0; i<1024; i++) qs[i] = 0;
    huffbook(&coder, qs, 4);
    assert(coder.book[0] == HCB_ZERO);

    // Values < 2
    coder.bandcnt = 1;
    qs[0] = 1;
    huffbook(&coder, qs, 4);
    assert(coder.book[1] == 1 || coder.book[1] == 2);

    // Values that require escape
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
