/*
 * Integration test for FAAD Decoder Library
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "faad.h"

int main(void)
{
    faad_params params;
    faad_status st = faad_params_init(&params, sizeof(params));
    assert(st == FAAD_OK);
    assert(params.stream_format == FAAD_STREAM_ADTS);

    faad_decoder *dec = NULL;
    st = faad_decoder_open(&params, NULL, 0, &dec);
    assert(st == FAAD_OK);
    assert(dec != NULL);

    faad_decoder_info info;
    info.struct_size = sizeof(info);
    st = faad_decoder_get_info(dec, &info);
    assert(st == FAAD_OK);
    assert(info.num_channels == 2);

    st = faad_decoder_close(&dec);
    assert(st == FAAD_OK);
    assert(dec == NULL);

    printf("FAAD decoder smoke test passed.\n");
    return 0;
}
