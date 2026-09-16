/*
 * Comprehensive Unit & Integration Test for FAAD3 Engine
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "faad.h"

int main(void)
{
    faad_config cfg;
    faad_status st = faad_config_init(&cfg, sizeof(cfg));
    assert(st == FAAD_OK);
    assert(cfg.stream_format == FAAD_STREAM_ADTS);

    uint32_t state_bytes = 0;
    st = faad_get_state_size(&cfg, &state_bytes);
    assert(st == FAAD_OK);
    assert(state_bytes > 0);

    /* Test 1: Static Memory Placement (Zero Heap Allocation) */
    void *static_mem = malloc(state_bytes);
    assert(static_mem != NULL);

    faad_decoder *dec_static = NULL;
    st = faad_decoder_init(static_mem, state_bytes, &cfg, NULL, 0, &dec_static);
    assert(st == FAAD_OK);
    assert(dec_static != NULL);

    faad_stream_info info;
    st = faad_decoder_get_info(dec_static, &info);
    assert(st == FAAD_OK);
    assert(info.channels == 2);

    st = faad_decoder_flush(dec_static);
    assert(st == FAAD_OK);

    free(static_mem);

    /* Test 2: Heap Wrapper Initialization */
    faad_decoder *dec_heap = NULL;
    st = faad_decoder_create(&cfg, NULL, 0, &dec_heap);
    assert(st == FAAD_OK);
    assert(dec_heap != NULL);

    st = faad_decoder_flush(dec_heap);
    assert(st == FAAD_OK);

    faad_decoder_destroy(dec_heap);

    printf("FAAD3 static placement and heap unit tests passed successfully.\n");
    return 0;
}
