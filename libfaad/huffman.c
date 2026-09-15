/*
 * Huffman decoder for spectral coefficients and scalefactors
 */

#include "faad_internal.h"
#include "sfb_tables.h"

static void setup_sfb_offsets(ICSInfo *ics, uint32_t sample_rate)
{
    int sr_idx = get_sr_index(sample_rate);
    if (ics->window_sequence == EIGHT_SHORT_SEQUENCE) {
        ics->num_sfbs = num_sfbs_128[sr_idx];
        const uint16_t *offsets = sfb_offsets_128[sr_idx];
        for (int i = 0; i <= ics->num_sfbs; i++) {
            ics->sfb_offsets[i] = offsets[i];
        }
    } else {
        ics->num_sfbs = num_sfbs_1024[sr_idx];
        const uint16_t *offsets = sfb_offsets_1024[sr_idx];
        for (int i = 0; i <= ics->num_sfbs; i++) {
            ics->sfb_offsets[i] = offsets[i];
        }
    }
}

static const hcode16_t * const huffbook_tables[] = {
    NULL, book01, book02, book03, book04, book05, book06, book07, book08, book09, book10, book11
};

static int decode_huffman_symbol(BitReader *bs, int book)
{
    if (book < 1 || book > 11) return 0;
    const hcode16_t *table = huffbook_tables[book];
    if (!table) return 0;

    uint32_t cw = 0;
    for (uint32_t len = 1; len <= 19; len++) {
        cw = (cw << 1) | bits_get(bs, 1);
        for (int i = 0; table[i].len != 0; i++) {
            if (table[i].len == len && table[i].data == cw) {
                return i;
            }
        }
    }
    return 0;
}

static void decode_quad(BitReader *bs, int book, int *v, int *w, int *x, int *y)
{
    int idx = decode_huffman_symbol(bs, book);
    int base = (book == 1 || book == 2) ? 3 : 5;
    *v = idx / (base * base * base);
    idx %= (base * base * base);
    *w = idx / (base * base);
    idx %= (base * base);
    *x = idx / base;
    *y = idx % base;

    if (book == 1 || book == 3) {
        if (*v) if (bits_get(bs, 1)) *v = -*v;
        if (*w) if (bits_get(bs, 1)) *w = -*w;
        if (*x) if (bits_get(bs, 1)) *x = -*x;
        if (*y) if (bits_get(bs, 1)) *y = -*y;
    }
}

static void decode_pair(BitReader *bs, int book, int *x, int *y)
{
    int idx = decode_huffman_symbol(bs, book);
    int base = 16;
    if (book == 5 || book == 6) base = 9;
    else if (book == 7 || book == 8) base = 13;
    else if (book == 9 || book == 10) base = 13;
    else if (book == 11) base = 17;

    *x = idx / base;
    *y = idx % base;

    if (book == 5 || book == 7 || book == 9 || book == 11) {
        if (*x) if (bits_get(bs, 1)) *x = -*x;
        if (*y) if (bits_get(bs, 1)) *y = -*y;
    }

    if (book == 11) {
        if (abs(*x) == 16) {
            int sign = (*x < 0) ? -1 : 1;
            int prefix = 0;
            while (bits_get(bs, 1) == 1) prefix++;
            int escape_val = (1 << (prefix + 4)) + bits_get(bs, prefix + 4);
            *x = sign * escape_val;
        }
        if (abs(*y) == 16) {
            int sign = (*y < 0) ? -1 : 1;
            int prefix = 0;
            while (bits_get(bs, 1) == 1) prefix++;
            int escape_val = (1 << (prefix + 4)) + bits_get(bs, prefix + 4);
            *y = sign * escape_val;
        }
    }
}

faad_status huffman_decode_spectrum(BitReader *bs, ICSInfo *ics, float *spec, uint32_t sample_rate)
{
    setup_sfb_offsets(ics, sample_rate);
    memset(spec, 0, FRAME_LEN_LONG * sizeof(float));

    int sf = ics->global_gain;
    int is_pos = 0;
    int pns_energy = sf;

    int window_offset = 0;
    for (int g = 0; g < ics->num_window_groups; g++) {
        for (int i = 0; i < ics->num_sections[g]; i++) {
            int cb = ics->sect_cb[g][i];
            int start_sfb = ics->sect_start[g][i];
            int end_sfb = ics->sect_end[g][i];

            if (cb == 0) {
                continue;
            } else if (cb == 13) { /* PNS */
                for (int sfb = start_sfb; sfb < end_sfb; sfb++) {
                    int dpns = decode_huffman_symbol(bs, 11);
                    pns_energy += dpns - 60;
                    ics->scalefactors[g][sfb] = pns_energy;
                    ics->pns_used[g][sfb] = true;
                }
            } else if (cb == 14 || cb == 15) { /* Intensity stereo (decoupled predictor) */
                for (int sfb = start_sfb; sfb < end_sfb; sfb++) {
                    int dis = decode_huffman_symbol(bs, 11);
                    is_pos += dis - 60;
                    ics->scalefactors[g][sfb] = is_pos;
                }
            } else {
                for (int sfb = start_sfb; sfb < end_sfb; sfb++) {
                    int dsf = decode_huffman_symbol(bs, 11);
                    sf += dsf - 60;
                    ics->scalefactors[g][sfb] = sf;
                    ics->sfb_cb[g][sfb] = cb;

                    int start_k = ics->sfb_offsets[sfb];
                    int end_k = ics->sfb_offsets[sfb + 1];

                    for (int w = 0; w < ics->window_group_length[g]; w++) {
                        float *ptr = spec + (window_offset + w) * 128 + start_k;
                        int k = start_k;
                        while (k < end_k) {
                            if (cb <= 4) {
                                int v, w_val, x, y;
                                decode_quad(bs, cb, &v, &w_val, &x, &y);
                                ptr[0] = (float)v;
                                ptr[1] = (float)w_val;
                                ptr[2] = (float)x;
                                ptr[3] = (float)y;
                                ptr += 4;
                                k += 4;
                            } else {
                                int x, y;
                                decode_pair(bs, cb, &x, &y);
                                ptr[0] = (float)x;
                                ptr[1] = (float)y;
                                ptr += 2;
                                k += 2;
                            }
                        }
                    }
                }
            }
        }
        window_offset += ics->window_group_length[g];
    }
    return FAAD_OK;
}
