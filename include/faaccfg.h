#ifndef _FAACCFG_H_
#define _FAACCFG_H_
#define FAAC_CFG_VERSION 105
#define MPEG2 1
#define MPEG4 0
#define MAIN 1
#define LOW  2
#define SSR  3
#define LTP  4
#define FAAC_INPUT_NULL    0
#define FAAC_INPUT_16BIT   1
#define FAAC_INPUT_24BIT   2
#define FAAC_INPUT_32BIT   3
#define FAAC_INPUT_FLOAT   4
#define SHORTCTL_NORMAL    0
#define SHORTCTL_NOSHORT   1
#define SHORTCTL_NOLONG    2
enum stream_format { RAW_STREAM = 0, ADTS_STREAM = 1 };
enum {JOINT_NONE = 0, JOINT_MS, JOINT_IS};
#pragma pack(push, 1)
typedef struct faacEncConfiguration {
    int version; char *name; char *copyright;
    unsigned int mpegVersion; unsigned int aacObjectType;
    union { unsigned int jointmode; unsigned int allowMidside; };
    unsigned int useLfe; unsigned int useTns;
    unsigned long bitRate; unsigned int bandWidth;
    unsigned long quantqual; unsigned int outputFormat;
    void *psymodellist; unsigned int psymodelidx;
    unsigned int inputFormat; int shortctl;
    int channel_map[64]; int pnslevel; int usePseudoSBR;
} faacEncConfiguration, *faacEncConfigurationPtr;
#pragma pack(pop)
#endif
