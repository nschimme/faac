#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

#include <faac.h>

static size_t g_alloc_count = 0;
static size_t g_free_count = 0;

static void *custom_alloc(size_t size)
{
    g_alloc_count++;
    /* Allocation wrapper simulating PSRAM or custom memory arena */
    return malloc(size);
}

static void custom_free(void *ptr)
{
    if (ptr) {
        g_free_count++;
        free(ptr);
    }
}

int main(void)
{
    faac_params params;
    faac_encoder *enc = NULL;
    faac_encoder_info info;
    faac_status status;

    printf("Testing FAAC custom memory allocator integration...\n");

    status = faac_params_init(&params, sizeof(params));
    assert(status == FAAC_OK);

    params.sample_rate = 44100;
    params.num_channels = 2;
    params.bit_rate = 64000;
    params.use_tns = false;
    params.alloc_func = custom_alloc;
    params.free_func = custom_free;

    status = faac_encoder_open(&params, &enc);
    assert(status == FAAC_OK);
    assert(enc != NULL);
    assert(g_alloc_count > 0);

    info.struct_size = sizeof(info);
    status = faac_encoder_get_info(enc, &info);
    assert(status == FAAC_OK);
    assert(info.frame_samples == 1024);

    /* Generate 1 frame of dummy 16-bit PCM audio (1024 samples/ch * 2 ch) */
    uint32_t num_samples = info.frame_samples * params.num_channels;
    int16_t *pcm_in = (int16_t *)calloc(num_samples, sizeof(int16_t));
    uint8_t out_buf[8192];
    uint32_t bytes_written = 0;

    status = faac_encoder_encode(enc, pcm_in, num_samples, out_buf, sizeof(out_buf), &bytes_written);
    if (status != FAAC_OK) {
        printf("faac_encoder_encode failed with status %d (%s)\n", status, faac_strerror(status));
        fflush(stdout);
    }
    assert(status == FAAC_OK);

    free(pcm_in);

    status = faac_encoder_close(&enc);
    assert(status == FAAC_OK);
    assert(enc == NULL);
    assert(g_alloc_count > 0);
    assert(g_alloc_count == g_free_count);

    printf("Custom allocator test passed! Allocs: %zu, Frees: %zu\n",
           g_alloc_count, g_free_count);

    return 0;
}
