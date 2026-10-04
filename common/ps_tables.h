/* Normative PS tables from ISO/IEC 14496-3. */
#ifndef PS_TABLES_H
#define PS_TABLES_H
#include <stdint.h>
#define PS_HUFF_IID_DF_FINE_OFFSET 30
#define PS_HUFF_IID_DF_FINE_NSYMS 61
typedef struct {
    uint32_t code : 24;
    uint32_t len : 8;
} SBRHuffEntry;
#define PS_HUFF_IID_DF_OFFSET 14
#define PS_HUFF_IID_DF_NSYMS 29
#define PS_HUFF_ICC_DF_OFFSET 7
#define PS_HUFF_ICC_DF_NSYMS 15
extern const SBRHuffEntry ps_huff_iid_df[PS_HUFF_IID_DF_NSYMS];
extern const SBRHuffEntry ps_huff_iid_df_fine[PS_HUFF_IID_DF_FINE_NSYMS];
#define PS_HUFF_IID_DT_FINE_OFFSET 30
#define PS_HUFF_IID_DT_FINE_NSYMS 61
extern const SBRHuffEntry ps_huff_iid_dt_fine[PS_HUFF_IID_DT_FINE_NSYMS];
extern const SBRHuffEntry ps_huff_icc_df[PS_HUFF_ICC_DF_NSYMS];
#define PS_HUFF_ICC_DT_OFFSET 7
#define PS_HUFF_ICC_DT_NSYMS 15
extern const SBRHuffEntry ps_huff_icc_dt[PS_HUFF_ICC_DT_NSYMS];
#define PS_HUFF_IPD_DF_NSYMS 8
#define PS_HUFF_OPD_DF_NSYMS 8
extern const SBRHuffEntry ps_huff_ipd_df[8];
#define PS_HUFF_IPD_DT_OFFSET 0
#define PS_HUFF_IPD_DT_NSYMS 8
extern const SBRHuffEntry ps_huff_ipd_dt[PS_HUFF_IPD_DT_NSYMS];
extern const SBRHuffEntry ps_huff_opd_df[8];
#define PS_HUFF_OPD_DT_OFFSET 0
#define PS_HUFF_OPD_DT_NSYMS 8
extern const SBRHuffEntry ps_huff_opd_dt[PS_HUFF_OPD_DT_NSYMS];
extern const int8_t ps_iid_db_default[15];
extern const int8_t ps_iid_db_fine[31];
extern const float ps_icc_invq[8];
#endif
