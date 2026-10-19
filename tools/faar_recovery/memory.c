/* Native glibc allocation accounting; diagnostic only, no whole-file storage. */
#define _GNU_SOURCE
#include "faar.h"
#include <dlfcn.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
static size_t live(struct mallinfo2 m) { return m.uordblks + m.hblkhd; }
int main(int argc,char **argv) {
    if(argc!=6){fprintf(stderr,"usage: memory libfaar.so in_rate out_rate channels seconds\n");return 2;}
    void *dll=dlopen(argv[1],RTLD_NOW);if(!dll){fprintf(stderr,"%s\n",dlerror());return 1;}
    faar_handle_t (*create)(uint32_t,uint32_t,uint16_t,faar_quality_t)=dlsym(dll,"faar_create");
    size_t (*process)(faar_handle_t,const float*,size_t,float*,size_t)=dlsym(dll,"faar_process");
    size_t (*flush)(faar_handle_t,float*,size_t)=dlsym(dll,"faar_flush");
    void (*destroy)(faar_handle_t)=dlsym(dll,"faar_destroy");
    if(!create||!process||!flush||!destroy)return 1;
    uint32_t ir=strtoul(argv[2],0,10),orate=strtoul(argv[3],0,10);uint16_t channels=strtoul(argv[4],0,10);
    size_t frames=(size_t)ir*strtoul(argv[5],0,10),block=4096,capacity=8192;
    if(!ir||!orate||!channels||channels>8)return 2;
    float *input=calloc(block*channels,sizeof(float)),*output=calloc(capacity*channels,sizeof(float));
    if(!input||!output)return 1;
    for(size_t i=0;i<block*channels;i++)input[i]=(float)(i%17)*.01f;
    malloc_trim(0);struct mallinfo2 before=mallinfo2();
    faar_handle_t h=create(ir,orate,channels,FAAR_QUALITY_HIGH);if(!h)return 1;
    struct mallinfo2 after=mallinfo2();size_t high=live(after),made=0;
    for(size_t at=0;at<frames;at+=block){size_t n=frames-at<block?frames-at:block;made+=process(h,input,n,output,capacity);
        for(;;){n=process(h,0,0,output,capacity);if(!n)break;made+=n;}
        size_t now=live(mallinfo2());if(now>high)high=now;
    }
    for(;;){size_t n=flush(h,output,capacity);if(!n)break;made+=n;size_t now=live(mallinfo2());if(now>high)high=now;}
    printf("{\"create_arena_delta_bytes\":%zu,\"create_mmap_delta_bytes\":%zu,\"create_total_delta_bytes\":%zu,\"sampled_retained_stream_high_water_delta_bytes\":%zu,\"client_buffer_bytes\":%zu,\"input_frames\":%zu,\"output_frames\":%zu}\n",after.uordblks-before.uordblks,after.hblkhd-before.hblkhd,live(after)-live(before),high-live(before),(block+capacity)*channels*sizeof(float),frames,made);
    destroy(h);free(input);free(output);
    /* Keep the DSO loaded through exit so Massif can resolve its allocation stacks. */
    return 0;
}
