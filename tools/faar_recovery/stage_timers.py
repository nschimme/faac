#!/usr/bin/env python3
"""Timing-only scratch wrappers around FAAR stages. Verify bit identity first."""
import argparse
import hashlib
import json
from pathlib import Path
import statistics
import subprocess
import numpy as np

REPO=Path(__file__).resolve().parents[2]

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--root',type=Path,default=Path('/tmp/faar-recovery'));a=p.parse_args();d=a.root/'stage-timers';d.mkdir(exist_ok=True)
    header='''#include <time.h>
extern double recovery_totals[3];
static double recovery_clock(void) { struct timespec t; clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&t); return t.tv_sec+t.tv_nsec*1e-9; }
'''
    (d/'timers.h').write_text(header)
    (d/'timers.c').write_text('double recovery_totals[3];\nvoid faar_recovery_reset(void) { for(int i=0;i<3;i++) recovery_totals[i]=0; }\nvoid faar_recovery_get(double *out) { for(int i=0;i<3;i++) out[i]=recovery_totals[i]; }\n')
    poly=(a.root/'reconstructed/polyresample.c').read_text()
    old='static size_t polyresample_process_single(faar_handle_t restrict r,'
    if old not in poly:raise RuntimeError('polyphase definition changed')
    poly=poly.replace(old,'static size_t recovery_polyphase_impl(faar_handle_t restrict r,')
    poly+='''\nstatic size_t polyresample_process_single(faar_handle_t r,const float *in,size_t count,float *out,size_t capacity) {
 double t=recovery_clock();size_t n=recovery_polyphase_impl(r,in,count,out,capacity);recovery_totals[1]+=recovery_clock()-t;return n;
}\n'''
    (d/'polyresample.c').write_text('#include "timers.h"\n'+poly)
    pre=(REPO/'libfaar/dft_prefilter.c').read_text()
    for name in ['process','flush']:
        pre=pre.replace('int faar_dft_prefilter_'+name+'(','int recovery_prefilter_'+name+'_impl(')
    pre+='''\nint faar_dft_prefilter_process(faar_dft_prefilter *s,const float *in,size_t frames,float *out,size_t cap,size_t *made) {
 double t=recovery_clock();int n=recovery_prefilter_process_impl(s,in,frames,out,cap,made);recovery_totals[0]+=recovery_clock()-t;return n;
}
int faar_dft_prefilter_flush(faar_dft_prefilter *s,float *out,size_t cap,size_t *made) {
 double t=recovery_clock();int n=recovery_prefilter_flush_impl(s,out,cap,made);recovery_totals[0]+=recovery_clock()-t;return n;
}\n'''
    (d/'dft_prefilter.c').write_text('#include "timers.h"\n'+pre)
    fft=(REPO/'libfaar/fft_double.c').read_text().replace('void faar_real_fft_execute(','void recovery_real_fft_impl(')
    fft+='''\nvoid faar_real_fft_execute(const faar_real_fft_plan *p,double *data,int inverse) {
 double t=recovery_clock();recovery_real_fft_impl(p,data,inverse);recovery_totals[2]+=recovery_clock()-t;
}\n'''
    (d/'fft_double.c').write_text('#include "timers.h"\n'+fft)
    cmd=['cc','-O3','-g','-flto','-fPIC','-shared','-DFAAR_BUILDING','-DFAAR_DOUBLE_COEFFICIENTS','-DFAAR_STAGED_BACKEND','-I'+str(REPO/'include'),'-I'+str(REPO/'libfaar'),str(d/'polyresample.c'),str(d/'dft_prefilter.c'),str(d/'fft_double.c'),str(d/'timers.c'),str(REPO/'libfaar/dft_fir.c'),str(REPO/'libfaar/dft_coeffs.c'),'-lm','-o',str(d/'libfaar.so')]
    subprocess.run(cmd,check=True)
    report={'build_command':cmd,'library_sha256':hashlib.sha256((d/'libfaar.so').read_bytes()).hexdigest(),'notes':'Stage timers are inclusive: real FFT time is contained within prefilter time. Clock-call overhead is included. No numerical or planner changes.','cases':[]}
    for ir,orate in [(96000,44100),(48000,44100),(44100,48000)]:
        t=np.arange(30*ir)/ir;source=np.column_stack([.25*np.sin(2*np.pi*997*t),.2*np.sin(2*np.pi*10000*t)]).astype(np.float32);source.tofile(d/'input.f32')
        def run(lib):
            r=subprocess.run([str(a.root/'driver'),'faar',str(lib),str(d/'input.f32'),str(d/'output.f32'),str(ir),str(orate),'2','4096','65536'],check=True,capture_output=True,text=True)
            return json.loads(r.stdout),hashlib.sha256((d/'output.f32').read_bytes()).hexdigest()
        _,expected=run(a.root/'reconstructed/libfaar.so');first,actual=run(d/'libfaar.so')
        if expected!=actual:raise RuntimeError('stage instrumentation changed output')
        runs=[run(d/'libfaar.so')[0] for _ in range(7)]
        keys=['processing_cpu_seconds','prefilter_cpu_seconds','polyphase_cpu_seconds','real_fft_cpu_seconds_in_prefilter']
        report['cases'].append({'in_rate':ir,'out_rate':orate,'bit_identity_vs_uninstrumented':True,'output_sha256':actual,'runs':runs,'medians':{key:statistics.median(x[key] for x in runs) for key in keys}})
    (a.root/'stage-timers.json').write_text(json.dumps(report,indent=2)+'\n');print(a.root/'stage-timers.json')

if __name__=='__main__':main()
