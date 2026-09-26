/* Probe-only FDK SBR decision importer.  Input is FAAD_DUMP's text format,
 * extended with RAW E=... Q=... A=... fields (see faad3 probe branch). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sbr_internal.h"

#define INJECT_FRAMES 16384
typedef struct InjectChannel {
    int seen, cls, le, lq, ptr, amp, harmflag;
    int freq[SBR_INJECT_MAX_ENVELOPES], t[SBR_INJECT_MAX_ENVELOPES+1];
    int e[SBR_INJECT_MAX_ENVELOPES][SBR_MAX_BANDS], q[2][SBR_MAX_BANDS];
    int invf[SBR_MAX_BANDS], harm[SBR_MAX_BANDS];
} InjectChannel;
typedef struct InjectFrame { int have_hdr, amp,start,stop,xover,scale,alter,noise; InjectChannel ch[2]; } InjectFrame;
struct SbrInject { unsigned fields; int offset; InjectFrame *f; int warned; };
enum { IF_HDR=1, IF_GRID=2, IF_ENV=4, IF_NOISE=8, IF_INVF=16, IF_HARM=32 };

static unsigned fields(const char *s) { unsigned x=0; char b[96], *p,*q; if (!s) return 0; snprintf(b,sizeof b,"%s",s); for(p=strtok_r(b,",",&q);p;p=strtok_r(NULL,",",&q)) { if(!strcmp(p,"hdr"))x|=IF_HDR; else if(!strcmp(p,"grid"))x|=IF_GRID; else if(!strcmp(p,"env"))x|=IF_ENV; else if(!strcmp(p,"noise"))x|=IF_NOISE; else if(!strcmp(p,"invf"))x|=IF_INVF; else if(!strcmp(p,"harm"))x|=IF_HARM; } return x; }
static void ints(const char *s, int *v, int n) { char b[4096],*p,*q; int i=0; if(!s)return; snprintf(b,sizeof b,"%s",s); for(p=strtok_r(b," ,/|",&q);p&&i<n;p=strtok_r(NULL," ,/|",&q))v[i++]=atoi(p); }
struct SbrInject *loadit(void) {
 const char *path=getenv("FAAC_SBR_INJECT"), *fs=getenv("FAAC_SBR_INJECT_FIELDS"); FILE *fp; char line[16384]; struct SbrInject *in; int ha=1,hs=15,ht=13,hx=0,hf=2,hl=0,hn=0;
 if(!path||!(fp=fopen(path,"r"))) return NULL; in=calloc(1,sizeof(*in)); if(!in){fclose(fp);return NULL;} in->fields=fs?fields(fs):IF_GRID; in->offset=(int)strtol(getenv("FAAC_SBR_INJECT_OFFSET")?getenv("FAAC_SBR_INJECT_OFFSET"):"0",NULL,0); in->f=calloc(INJECT_FRAMES,sizeof(*in->f)); if(!in->f){fclose(fp);free(in);return NULL;}
 while(fgets(line,sizeof line,fp)) { unsigned fr,ch; if(line[0]=='H' && sscanf(line,"H %u %*d %d %d %d %d %d %d %d",&fr,&ha,&hs,&ht,&hx,&hf,&hl,&hn)==8) { if(fr<INJECT_FRAMES){InjectFrame *z=&in->f[fr];z->have_hdr=1;z->amp=ha;z->start=hs;z->stop=ht;z->xover=hx;z->scale=hf;z->alter=hl;z->noise=hn;} }
 else if(line[0]=='F' && sscanf(line,"F %u %u",&fr,&ch)==2 && fr<INJECT_FRAMES && ch<2) { InjectChannel *c=&in->f[fr].ch[ch]; int x; if(sscanf(line,"F %u %u %d %d %d",&fr,&ch,&c->cls,&c->le,&x)==5)c->seen=1; }
 else if(line[0]=='R' && sscanf(line,"R %u %u",&fr,&ch)==2 && fr<INJECT_FRAMES && ch<2) { InjectChannel*c=&in->f[fr].ch[ch]; char *p; int tmp[SBR_MAX_BANDS]; c->seen=1; p=strstr(line,"freq_res:"); if(p) ints(p+9,c->freq,SBR_INJECT_MAX_ENVELOPES); p=strstr(line,"tE:"); if(p){ints(p+3,c->t,SBR_INJECT_MAX_ENVELOPES+1); c->le=0; while(c->le<SBR_INJECT_MAX_ENVELOPES && c->t[c->le+1]) c->le++;} p=strstr(line,"tQ:"); if(p){ints(p+3,tmp,3);c->lq=tmp[2]?2:1;} p=strstr(line,"invf:");if(p)ints(p+5,c->invf,SBR_MAX_BANDS); p=strstr(line,"harm:");if(p){ints(p+5,tmp,SBR_MAX_BANDS);c->harmflag=tmp[0];for(int k=0;k<SBR_MAX_BANDS-1;k++)c->harm[k]=tmp[k+1];} p=strstr(line,"E:");if(p)ints(p+2,&c->e[0][0],SBR_INJECT_MAX_ENVELOPES*SBR_MAX_BANDS);p=strstr(line,"Q:");if(p)ints(p+2,&c->q[0][0],2*SBR_MAX_BANDS); }
 else if(line[0]=='G' && sscanf(line,"G %u %u",&fr,&ch)==2 && fr<INJECT_FRAMES && ch<2) { InjectChannel*c=&in->f[fr].ch[ch]; char *p=line; int cls,le,ptr; if(sscanf(line,"G %u %u %d %d %d",&fr,&ch,&cls,&le,&ptr)==5){c->cls=cls;c->le=le;c->ptr=ptr;for(int i=0;i<6&&p;i++){p=strchr(p,' ');if(p)p++;}for(int i=0;p&&i<=SBR_MAX_ENVELOPES;i++,p=strchr(p,' '))c->t[i]=atoi(p);} }
 } fclose(fp); return in;
}
int SbrInjectGetGrid(SBRInfo *s, SignalAnalysis *sa, int stream_frame) {
    struct SbrInject *in = s ? s->inject : NULL;
    if (!in || !(in->fields & IF_GRID)) return 0;
    /* frameCount names the access unit currently leaving the core FIFO;
       this analysis result is emitted after the four-slot SBR payload ring. */
    int n = stream_frame + in->offset + SBR_FRAME_FIFO;
    if (n < 0 || n >= INJECT_FRAMES || !in->f[n].ch[0].seen) return 0;
    for (int ch = 0; ch < s->numChannels; ch++) {
        InjectChannel *c = &in->f[n].ch[ch];
        SbrAnalysisGrid *g = &sa->grid[ch];
        if (!c->seen) return 0;
        g->frameClass = c->cls;
        g->numEnvelopes = c->le;
        g->bsPointer = c->ptr;
        for (int i = 0; i <= c->le; i++) g->tEnv[i] = c->t[i];
        for (int i = 0; i < c->le; i++) g->freqResEnv[i] = c->freq[i];
    }
    return 1;
}

int SbrInjectApply(SBRInfo *s, SbrFrameData *fd, int stream_frame, int channels) { struct SbrInject *in=s->inject; if (!in) return 0; int n=stream_frame+in->offset+SBR_FRAME_FIFO; if(n<0||n>=INJECT_FRAMES||!in->f[n].ch[0].seen)return 0; InjectFrame*z=&in->f[n]; for(int ch=0;ch<channels;ch++){InjectChannel*c=&z->ch[ch];if(!c->seen)continue;if(in->fields&IF_ENV)memcpy(fd->ch[ch].envData,c->e,sizeof(c->e));if(in->fields&IF_NOISE)memcpy(fd->ch[ch].noiseData,c->q,sizeof(c->q));if(in->fields&IF_INVF)memcpy(fd->ch[ch].invfMode,c->invf,sizeof(c->invf));if(in->fields&IF_HARM){fd->ch[ch].addHarmonicFlag=c->harmflag;memcpy(fd->ch[ch].addHarmonic,c->harm,sizeof(c->harm));}} return 1; }
void SbrInjectFree(struct SbrInject *in){if(in){free(in->f);free(in);}}
