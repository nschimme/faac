#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "frame.h"
#include "coder.h"
#include "channels.h"
#include "bitstream.h"
#include "filtbank.h"
#include "util.h"
#include "tns.h"
#include "stereo.h"
#include "pseudo_sbr.h"
#if (defined WIN32 || defined _WIN32 || defined WIN64 || defined _WIN64) && !defined(PACKAGE_VERSION)
#include "win32_ver.h"
#endif
static char *libfaacName = PACKAGE_VERSION;
static char *libCopyright = "FAAC - Freeware Advanced Audio Coder\n";
static const psymodellist_t psymodellist[] = { {&psymodel2, "knipsycho psychoacoustic"}, {NULL} };
static SR_INFO srInfo[12+1];
static unsigned int CalcBandwidth(unsigned long br, unsigned long sr) {
    const unsigned int nyq = sr / 2; unsigned int bw; if (!br) return nyq;
    if (br <= 16000) bw = 4000 + (br / 8);
    else if (br <= 32000) bw = 6000 + ((br - 16000) * 5 / 16);
    else if (br <= 64000) bw = 10000 + ((br - 32000) * 15 / 64);
    else if (br <= 128000) bw = 18500 + ((br - 64000) * 3 / 128);
    else bw = 20000;
    return (bw > nyq) ? nyq : bw;
}
int FAACAPI faacEncGetVersion(char **id, char **cp) { if(id)*id=libfaacName; if(cp)*cp=libCopyright; return FAAC_CFG_VERSION; }
int FAACAPI faacEncGetDecoderSpecificInfo(faacEncHandle h, unsigned char** p, unsigned long* s) {
    faacEncStruct* hE = (faacEncStruct*)h; if(!hE||!p||!s) return -1; if(hE->config.mpegVersion==MPEG2) return -2;
    *s=2; *p=malloc(2); if(*p) { BitStream* b=OpenBitStream(2,*p); PutBit(b,hE->config.aacObjectType,5); PutBit(b,hE->sampleRateIdx,4); PutBit(b,hE->numChannels,4); CloseBitStream(b); return 0; } return -3;
}
faacEncConfigurationPtr FAACAPI faacEncGetCurrentConfiguration(faacEncHandle h) { return &((faacEncStruct*)h)->config; }
int FAACAPI faacEncSetConfiguration(faacEncHandle h, faacEncConfigurationPtr c) {
    faacEncStruct* hE = (faacEncStruct*)h; int i, maxq = hE->config.outputFormat ? MAXQUALADTS : MAXQUAL;
    hE->config.jointmode=c->jointmode; hE->config.useLfe=c->useLfe; hE->config.useTns=c->useTns; hE->config.usePseudoSBR=c->usePseudoSBR;
    hE->config.aacObjectType=c->aacObjectType; hE->config.mpegVersion=c->mpegVersion; hE->config.outputFormat=c->outputFormat;
    hE->config.inputFormat=c->inputFormat; hE->config.shortctl=c->shortctl;
    if(hE->config.aacObjectType!=LOW) return 0;
    TnsInit(hE);
    if(!hE->sampleRate||!hE->numChannels) return 0;
    if(c->bitRate>(MaxBitrate(hE->sampleRate)/hE->numChannels)) c->bitRate=MaxBitrate(hE->sampleRate)/hE->numChannels;
    if(c->bitRate&&!c->bandWidth) c->bandWidth=CalcBandwidth(c->bitRate,hE->sampleRate);
    if(!c->quantqual) {
        c->quantqual=(faac_real)c->bitRate*hE->numChannels/1280;
        if(c->quantqual>100) c->quantqual=(c->quantqual-100)*3.0+100;
    }
    if(!c->quantqual) c->quantqual=DEFQUAL;
    hE->config.bitRate=c->bitRate;
    hE->config.bandWidth=c->bandWidth;
    if(hE->config.bandWidth<100) hE->config.bandWidth=100;
    if(hE->config.bandWidth>(hE->sampleRate/2)) hE->config.bandWidth=hE->sampleRate/2;
    if(c->quantqual>maxq) c->quantqual=maxq;
    if(c->quantqual<MINQUAL) c->quantqual=MINQUAL;
    hE->config.quantqual=c->quantqual;
    hE->aacquantCfg.pnslevel=(c->mpegVersion==MPEG2)?0:((c->pnslevel<0)?0:((c->pnslevel>10)?10:c->pnslevel));
    hE->aacquantCfg.quality=c->quantqual; CalcBW(&hE->config.bandWidth, hE->sampleRate, hE->srInfo, &hE->aacquantCfg);
    hE->psymodel->PsyEnd(&hE->gpsyInfo, hE->psyInfo, hE->numChannels);
    if(c->psymodelidx>=(sizeof(psymodellist)/sizeof(psymodellist[0])-1)) c->psymodelidx=(sizeof(psymodellist)/sizeof(psymodellist[0]))-2;
    hE->config.psymodelidx=c->psymodelidx; hE->psymodel=(psymodel_t*)psymodellist[hE->config.psymodelidx].ptr;
    hE->psymodel->PsyInit(&hE->gpsyInfo,hE->psyInfo,hE->numChannels,hE->sampleRate,hE->srInfo->cb_width_long,hE->srInfo->num_cb_long,hE->srInfo->cb_width_short,hE->srInfo->num_cb_short);
    for(i=0;i<MAX_CHANNELS;i++) hE->config.channel_map[i]=c->channel_map[i];
    return 1;
}
faacEncHandle FAACAPI faacEncOpen(unsigned long sr, unsigned int nc, unsigned long *is, unsigned long *mob) {
    faacEncStruct* h; if(nc>MAX_CHANNELS) return NULL; *is=FRAME_LEN*nc; *mob=ADTS_FRAMESIZE;
    h=AllocMemory(sizeof(faacEncStruct)); memset(h,0,sizeof(faacEncStruct));
    h->numChannels=nc; h->sampleRate=sr; h->sampleRateIdx=GetSRIndex(sr); h->config.version=FAAC_CFG_VERSION;
    h->config.name=libfaacName; h->config.copyright=libCopyright; h->config.mpegVersion=MPEG4; h->config.aacObjectType=LOW;
    h->config.jointmode=JOINT_IS; h->config.pnslevel=4; h->config.useLfe=1; h->config.usePseudoSBR=-1;
    h->sbrRandState=0xDEADBEEFu^(uint32_t)(sr*31337u); h->config.bitRate=64000; h->config.bandWidth=CalcBandwidth(64000,sr);
    h->config.psymodelidx=0; h->psymodel=(psymodel_t*)psymodellist[0].ptr;
    h->config.shortctl=SHORTCTL_NORMAL; for(int j=0;j<MAX_CHANNELS;j++) h->config.channel_map[j]=j;
    h->config.outputFormat=ADTS_STREAM; h->config.inputFormat=FAAC_INPUT_32BIT; h->srInfo=&srInfo[h->sampleRateIdx];
    for(int j=0;j<nc;j++){ h->coderInfo[j].prev_window_shape=h->coderInfo[j].window_shape=SINE_WINDOW; h->coderInfo[j].block_type=ONLY_LONG_WINDOW; h->coderInfo[j].groups.n=1; h->coderInfo[j].groups.len[0]=1; }
    fft_initialize(&h->fft_tables); h->psymodel->PsyInit(&h->gpsyInfo,h->psyInfo,nc,sr,h->srInfo->cb_width_long,h->srInfo->num_cb_long,h->srInfo->cb_width_short,h->srInfo->num_cb_short);
    FilterBankInit(h); TnsInit(h); QuantizeInit(); return h;
}
int FAACAPI faacEncClose(faacEncHandle h) {
    faacEncStruct* hE=(faacEncStruct*)h; hE->psymodel->PsyEnd(&hE->gpsyInfo,hE->psyInfo,hE->numChannels); FilterBankEnd(hE); fft_terminate(&hE->fft_tables);
    for(int j=0;j<hE->numChannels;j++){ if(hE->sampleBuff[j]) FreeMemory(hE->sampleBuff[j]); if(hE->next3SampleBuff[j]) FreeMemory(hE->next3SampleBuff[j]); }
    FreeMemory(hE); BlocStat(); return 0;
}
int FAACAPI faacEncEncode(faacEncHandle hp, int32_t *ib, unsigned int si, unsigned char *ob, unsigned int bs) {
    faacEncStruct* h=(faacEncStruct*)hp; unsigned int c, i, off; int sb, fb; BitStream *bts; h->frameNum++; if(si==0) h->flushFrame++;
    if(h->flushFrame>4) return 0;
    bts=OpenBitStream(bs,ob);
    if (!bts) return -1;
    GetChannelInfo(h->channelInfo,h->numChannels,h->config.useLfe);
    for(c=0;c<h->numChannels;c++){
        faac_real *t; if(!h->sampleBuff[c]) h->sampleBuff[c]=AllocMemory(FRAME_LEN*sizeof(faac_real));
        t=h->sampleBuff[c]; h->sampleBuff[c]=h->next3SampleBuff[c]; h->next3SampleBuff[c]=t;
        if(si==0) { for(i=0;i<FRAME_LEN;i++) h->next3SampleBuff[c][i]=0; }
        else {
            int spc=si/h->numChannels;
            if(h->config.inputFormat==FAAC_INPUT_16BIT){ short *ic=(short*)ib+h->config.channel_map[c]; for(i=0;i<spc;i++){ h->next3SampleBuff[c][i]=(faac_real)*ic; ic+=h->numChannels; } }
            else if(h->config.inputFormat==FAAC_INPUT_32BIT){ int32_t *ic=ib+h->config.channel_map[c]; for(i=0;i<spc;i++){ h->next3SampleBuff[c][i]=(1.0/256)*(faac_real)*ic; ic+=h->numChannels; } }
            else { float *ic=(float*)ib+h->config.channel_map[c]; for(i=0;i<spc;i++){ h->next3SampleBuff[c][i]=(faac_real)*ic; ic+=h->numChannels; } }
            for(i=spc;i<FRAME_LEN;i++) h->next3SampleBuff[c][i]=0;
        }
        if(h->channelInfo[c].type!=ELEMENT_LFE) h->psymodel->PsyBufferUpdate(&h->fft_tables,&h->gpsyInfo,&h->psyInfo[c],h->next3SampleBuff[c],h->config.bandWidth,h->srInfo->cb_width_short,h->srInfo->num_cb_short);
    }
    if(h->frameNum<=3) {
        CloseBitStream(bts);
        return 0;
    }
    h->psymodel->PsyCalculate(h->channelInfo,&h->gpsyInfo,h->psyInfo,h->srInfo->cb_width_long,h->srInfo->num_cb_long,h->srInfo->cb_width_short,h->srInfo->num_cb_short,h->numChannels,(faac_real)h->aacquantCfg.quality/DEFQUAL);
    h->psymodel->BlockSwitch(h->coderInfo,h->psyInfo,h->numChannels);
    if(h->config.shortctl==SHORTCTL_NOSHORT) for(c=0;c<h->numChannels;c++) h->coderInfo[c].block_type=ONLY_LONG_WINDOW;
    else if(h->frameNum<=4||h->config.shortctl==SHORTCTL_NOLONG) for(c=0;c<h->numChannels;c++) h->coderInfo[c].block_type=ONLY_SHORT_WINDOW;
    for(c=0;c<h->numChannels;c++) FilterBank(h,&h->coderInfo[c],h->sampleBuff[c],h->freqBuff[c],h->overlapBuff[c],MOVERLAPPED);
    for(c=0;c<h->numChannels;c++){
        h->channelInfo[c].msInfo.is_present=0;
        if(h->coderInfo[c].block_type==ONLY_SHORT_WINDOW){
            h->coderInfo[c].sfbn=h->aacquantCfg.max_cbs; off=0;
            for(sb=0;sb<h->coderInfo[c].sfbn;sb++){ h->coderInfo[c].sfb_offset[sb]=off; off+=h->srInfo->cb_width_short[sb]; }
            h->coderInfo[c].sfb_offset[sb]=off; BlocGroup(h->freqBuff[c],&h->coderInfo[c],&h->aacquantCfg);
        } else {
            h->coderInfo[c].sfbn=h->aacquantCfg.max_cbl; h->coderInfo[c].groups.n=1; h->coderInfo[c].groups.len[0]=1; off=0;
            for(sb=0;sb<h->coderInfo[c].sfbn;sb++){ h->coderInfo[c].sfb_offset[sb]=off; off+=h->srInfo->cb_width_long[sb]; }
            h->coderInfo[c].sfb_offset[sb]=off;
        }
    }
    for(c=0;c<h->numChannels;c++) if(h->channelInfo[c].type!=ELEMENT_LFE&&h->config.useTns) TnsEncode(&h->coderInfo[c].tnsInfo,h->coderInfo[c].sfbn,h->coderInfo[c].sfbn,h->coderInfo[c].block_type,h->coderInfo[c].sfb_offset,h->freqBuff[c],h->gpsyInfo.sharedWorkBuffLong); else h->coderInfo[c].tnsInfo.tnsDataPresent=0;
    AACstereo(h->coderInfo,h->channelInfo,h->freqBuff,h->numChannels,(faac_real)h->aacquantCfg.quality/DEFQUAL,h->config.jointmode);
    for(c=0;c<h->numChannels;c++){
        if(h->channelInfo[c].present&&h->channelInfo[c].type==ELEMENT_CPE&&h->channelInfo[c].ch_is_left){
            CoderInfo *cl=&h->coderInfo[c], *cr=&h->coderInfo[h->channelInfo[c].paired_ch];
            if(cl->sfbn!=cr->sfbn){
                CoderInfo *ci=(cl->sfbn<cr->sfbn)?cl:cr; int t=(cl->sfbn>cr->sfbn)?cl->sfbn:cr->sfbn;
                int *w=(ci->block_type==ONLY_SHORT_WINDOW)?h->srInfo->cb_width_short:h->srInfo->cb_width_long;
                int m=(ci->block_type==ONLY_SHORT_WINDOW)?NSFB_SHORT:NSFB_LONG;
                while(ci->sfbn<t&&ci->sfbn<m){ ci->sfb_offset[ci->sfbn+1]=ci->sfb_offset[ci->sfbn]+w[ci->sfbn]; ci->sfbn++; }
                cl->sfbn=cr->sfbn=t;
            }
        }
    }
    if(h->config.bitRate){
        int uS=h->config.usePseudoSBR;
        unsigned long bc = h->numChannels > 0 ? (h->config.bitRate / h->numChannels) : 0;
        if(uS==-1)uS=(bc<48000);
        if(uS&&PseudoSBRShouldEnable(h->config.bandWidth,h->sampleRate,bc)){
            unsigned int tBW=PseudoSBRTargetBW(h->config.bandWidth,h->sampleRate,bc);
            if(tBW>=h->config.bandWidth+SBR_MIN_EXTENSION)
                for(c=0;c<h->numChannels;c++)
                    if(h->channelInfo[c].type!=ELEMENT_LFE)
                        PseudoSBR(&h->coderInfo[c],h->freqBuff[c],h->sampleRate,h->config.bandWidth,tBW,bc,h->srInfo->cb_width_long,h->srInfo->num_cb_long,&h->sbrRandState);
        }
    }
    for(c=0;c<h->numChannels;c++) { h->aacquantCfg.target_bits=(int)(h->config.bitRate*FRAME_LEN/h->sampleRate); BlocQuant(&h->coderInfo[c],h->freqBuff[c],&h->aacquantCfg); }
    if(WriteBitstream(h,h->coderInfo,h->channelInfo,bts,h->numChannels)<0) return -1;
    fb=CloseBitStream(bts);
    if(h->config.bitRate){
        int db=h->numChannels*(h->config.bitRate*FRAME_LEN)/h->sampleRate; faac_real fx=(faac_real)db/(fb*8);
        if(fx>1.005||fx<0.995){ fx=(fx-1.0)*0.5+1.0; h->aacquantCfg.quality*=fx; }
        if(h->aacquantCfg.quality>MAXQUALADTS) h->aacquantCfg.quality=MAXQUALADTS;
        if(h->aacquantCfg.quality<MINQUAL) h->aacquantCfg.quality=MINQUAL;
    }
    return fb;
}
static SR_INFO srInfo[12+1] = {
    { 96000, 41, 12, {4,4,4,4,4,4,4,4,4,4,4,4,4,4,8,8,8,8,8,12,12,12,12,12,16,16,24,28,36,44,64,64,64,64,64,64,64,64,64,64,64}, {4,4,4,4,4,4,8,8,8,16,28,36} },
    { 88200, 41, 12, {4,4,4,4,4,4,4,4,4,4,4,4,4,4,8,8,8,8,8,12,12,12,12,12,16,16,24,28,36,44,64,64,64,64,64,64,64,64,64,64,64}, {4,4,4,4,4,4,8,8,8,16,28,36} },
    { 64000, 47, 12, {4,4,4,4,4,4,4,4,4,4,4,4,4,4,8,8,8,8,12,12,12,16,16,16,20,24,24,28,36,40,40,40,40,40,40,40,40,40,40,40,40,40,40,40,40,40,40}, {4,4,4,4,4,4,8,8,8,16,28,32} },
    { 48000, 49, 14, {4,4,4,4,4,4,4,4,4,4,8,8,8,8,8,8,8,12,12,12,12,16,16,20,20,24,24,28,28,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,96}, {4,4,4,4,4,8,8,8,12,12,12,16,16,16} },
    { 44100, 49, 14, {4,4,4,4,4,4,4,4,4,4,8,8,8,8,8,8,8,12,12,12,12,16,16,20,20,24,24,28,28,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,96}, {4,4,4,4,4,8,8,8,12,12,12,16,16,16} },
    { 32000, 51, 14, {4,4,4,4,4,4,4,4,4,4,8,8,8,8,8,8,8,12,12,12,12,16,16,20,20,24,24,28,28,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32}, {4,4,4,4,4,8,8,8,12,12,12,16,16,16} },
    { 24000, 47, 15, {4,4,4,4,4,4,4,4,4,4,4,8,8,8,8,8,8,8,8,8,8,12,12,12,12,16,16,20,20,24,24,28,28,32,36,36,40,44,48,52,52,64,64,64,64,64}, {4,4,4,4,4,4,4,8,8,8,12,12,16,16,20} },
    { 22050, 47, 15, {4,4,4,4,4,4,4,4,4,4,4,8,8,8,8,8,8,8,8,8,8,12,12,12,12,16,16,20,20,24,24,28,28,32,36,36,40,44,48,52,52,64,64,64,64,64}, {4,4,4,4,4,4,4,8,8,8,12,12,16,16,20} },
    { 16000, 43, 15, {8,8,8,8,8,8,8,8,8,8,8,12,12,12,12,12,12,12,12,12,16,16,16,16,20,20,20,24,24,28,28,32,36,40,40,44,48,52,56,60,64,64,64}, {4,4,4,4,4,4,4,4,8,8,12,12,16,20,20} },
    { 12000, 43, 15, {8,8,8,8,8,8,8,8,8,8,8,12,12,12,12,12,12,12,12,12,16,16,16,16,20,20,20,24,24,28,28,32,36,40,40,44,48,52,56,60,64,64,64}, {4,4,4,4,4,4,4,4,8,8,12,12,16,20,20} },
    { 11025, 43, 15, {8,8,8,8,8,8,8,8,8,8,8,12,12,12,12,12,12,12,12,12,16,16,16,16,20,20,20,24,24,28,28,32,36,40,40,44,48,52,56,60,64,64,64}, {4,4,4,4,4,4,4,4,8,8,12,12,16,20,20} },
    { 8000, 40, 15, {12,12,12,12,12,12,12,12,12,12,12,12,12,16,16,16,16,16,16,16,20,20,20,20,24,24,24,28,28,32,36,36,40,44,48,52,56,60,64,80}, {4,4,4,4,4,4,4,8,8,8,8,12,16,20,20} },
    { -1 }
};
