#!/usr/bin/env python3
"""Adapter parity, analyzer controls, numerical-floor proposal, footprint accounting."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from compare_faar_ardft import measure

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def save(p,x):p.write_text(json.dumps(x,indent=2,sort_keys=True)+'\n')
def wav_write(path,source,rate):
    channels=source.shape[1];raw=source.astype('<f4').tobytes();fmt=struct.pack('<HHIIHH',3,channels,rate,rate*channels*4,channels*4,32);chunks=b'fmt '+struct.pack('<I',16)+fmt+b'data'+struct.pack('<I',len(raw))+raw
    path.write_bytes(b'RIFF'+struct.pack('<I',len(chunks)+4)+b'WAVE'+chunks)
def wav_read(path,channels):
    data=path.read_bytes();pos=12
    while pos+8<=len(data):
        tag=data[pos:pos+4];size=struct.unpack_from('<I',data,pos+4)[0]
        if tag==b'data':return np.frombuffer(data,dtype='<f4',count=size//4,offset=pos+8).copy().reshape(-1,channels)
        pos+=8+size+(size&1)
    raise RuntimeError('missing data chunk')

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--root',type=Path,default=Path('/tmp/faar-recovery'));p.add_argument('--original-kbsrc',type=Path,default=Path('/tmp/faar-recovery/kbsrc-portable/kbsrc'));p.add_argument('--original-sox',type=Path);p.add_argument('--original-ardft',type=Path);a=p.parse_args();a.original_sox=a.original_sox or (a.root/'sox' if (a.root/'sox').exists() else a.root/'sox-native-cli');a.original_ardft=a.original_ardft or (Path('/tmp/ardftsrc-bench/ardftsrc-bench') if Path('/tmp/ardftsrc-bench/ardftsrc-bench').exists() else a.root/'ardftsrc-uninstrumented');work=a.root/'validation';work.mkdir(exist_ok=True)
    report={'metadata':{'driver_sha256':sha(a.root/'driver'),'validator_sha256':sha(__file__),'original_sox_sha256':sha(a.original_sox),'original_kbsrc_sha256':sha(a.original_kbsrc),'original_ardft_sha256':sha(a.original_ardft)},'adapter_parity':[],'controls':[],'footprint':[]}
    for ir,orate in [(96000,44100),(48000,44100),(44100,48000)]:
        t=np.arange(ir)/ir;source=np.column_stack([.2*np.sin(2*np.pi*997*t),.2*np.sin(2*np.pi*10000*t)]).astype(np.float32);source.tofile(work/'input.f32');wav_write(work/'input.wav',source,ir)
        for kind,variant in [('sox','default'),('sox','wide'),('kbsrc-fast','generic'),('kbsrc-fast','auto'),('kbsrc-exact','generic')]:
            driver=[str(a.root/'driver'),kind,variant,str(work/'input.f32'),str(work/'driver.f32'),str(ir),str(orate),'2','4096','65536']
            subprocess.run(driver,check=True,capture_output=True)
            if kind=='sox':
                cmd=[str(a.original_sox),'--single-threaded','-t','raw','-e','float','-b','32','-r',str(ir),'-c','2',str(work/'input.f32'),'-t','raw','-e','float','-b','32',str(work/'reference.f32'),'rate','-v']+(['-b','99.5'] if variant=='wide' else [])+[str(orate)]
                subprocess.run(cmd,check=True,capture_output=True);ref=np.fromfile(work/'reference.f32',dtype='<f4').reshape(-1,2)
            else:
                cmd=[str(a.original_kbsrc),'--mode','fast' if kind=='kbsrc-fast' else 'exact','--threads','1','--no-lpc','--format','f32','--rate',str(orate),str(work/'input.wav'),str(work/'reference.wav')]
                subprocess.run(cmd,check=True,capture_output=True,env=dict(os.environ,KBSRC_CPU=variant));ref=wav_read(work/'reference.wav',2)
            out=np.fromfile(work/'driver.f32',dtype='<f4').reshape(-1,2)
            if out.shape!=ref.shape:raise RuntimeError('adapter output shape differs')
            delta=float(np.max(abs(out-ref)));report['adapter_parity'].append({'engine':kind,'variant':variant,'in_rate':ir,'out_rate':orate,'bit_identity':out.tobytes()==ref.tobytes(),'max_abs_error':delta,'driver_command':driver,'reference_command':cmd})
            if delta>6e-8:raise RuntimeError('adapter changed reference output')
        outputs=[]
        for binary in [a.root/'ardftsrc',a.original_ardft]:
            cmd=[str(binary),str(work/'input.wav'),str(work/'ardft.wav'),str(orate),'32','16384','0.99'];subprocess.run(cmd,check=True,capture_output=True);outputs.append(wav_read(work/'ardft.wav',2))
        error=float(np.max(abs(outputs[0]-outputs[1])));report['adapter_parity'].append({'engine':'ardftsrc','in_rate':ir,'out_rate':orate,'bit_identity':outputs[0].tobytes()==outputs[1].tobytes(),'max_abs_error':error})
        if error>6e-8:raise RuntimeError('reference compiler/instrumentation changed output excessively')
    # Analytic controls are double samples; expected values do not come from a resampler.
    ir,orate=96000,44100;peak=.5;frequency=1000
    source=peak*np.sin(2*np.pi*frequency*np.arange(ir*3)/ir);t=np.arange(orate*3)/orate
    y=peak*.99*np.sin(2*np.pi*frequency*t+.3)
    gain=measure(y,source,ir,orate,frequency,peak)['wanted_gain_db'];expected=20*math.log10(.99)
    if abs(gain-expected)>1e-8:raise RuntimeError('analytic gain control failed')
    report['controls'].append({'control':'gain and phase','actual_gain_db':gain,'expected_gain_db':expected})
    y=peak*np.sin(2*np.pi*frequency*t)+peak*1e-4*np.sin(2*np.pi*1234*t)
    spur=measure(y,source,ir,orate,frequency,peak)['residual_relative_input_db']
    if abs(spur-(-80))>1e-6:raise RuntimeError('analytic spur control failed')
    report['controls'].append({'control':'known spur','actual_residual_relative_db':spur,'expected_residual_relative_db':-80})
    # Independent Fourier interpolation of quantized PCM, with 70-digit direct sums.
    from decimal import Decimal as D, localcontext
    from oracle import PI, sin
    with localcontext() as context:
        context.prec=70; N=64; M=80
        samples=[D.from_float(float(np.float32(float(D('.5')*sin(2*PI*3*j/N))))) for j in range(N)]
        bins=[]
        for k in range(N//2+1):
            real=sum(samples[j]*sin(2*PI*k*j/N+PI/2) for j in range(N))
            imag=-sum(samples[j]*sin(2*PI*k*j/N) for j in range(N))
            bins.append((real,imag))
        exact=[]
        for j in range(M):
            value=bins[0][0]
            for k in range(1,N//2+1):
                weight=1 if k==N//2 else 2
                value+=weight*(bins[k][0]*sin(2*PI*k*j/M+PI/2)-bins[k][1]*sin(2*PI*k*j/M))
            exact.append(float(value/N))
        x=np.array([float(v) for v in samples]);X=np.zeros(M//2+1,complex);X[:N//2+1]=np.fft.rfft(x);X[N//2]*=.5
        numeric=np.fft.irfft(X,n=M)*M/N;exact=np.array(exact)
        error=float(np.max(abs(exact-numeric)))
        if error>2e-14:raise RuntimeError('independent spectral-projection control failed')
        projection=measure(np.tile(exact.astype(np.float32).astype(float),3),np.tile(x,3),N,M,3,.5)
        report['controls'].append({'control':'70-digit direct DFT Fourier interpolation of float32 quantized PCM, 64 to 80 samples','max_abs_error_numpy_vs_decimal':error,'float32_output_bit_identity':exact.astype(np.float32).tobytes()==numeric.astype(np.float32).tobytes(),'residual_relative_input_db':projection['residual_relative_input_db']})
    signals=json.loads((a.root/'signals.json').read_text())
    floors=[x['ideal_float32_rounding']['residual_relative_input_db'] for x in signals['floor_controls'] if x['frequency_hz'] in [1000,10000,20000]]
    source_floors=[x['input_rounding_relative_rms_db'] for x in signals['floor_controls'] if x['frequency_hz'] in [1000,10000,20000]]
    floor=max(floors+source_floors+[projection['residual_relative_input_db']]);ceiling=floor+6
    report['numerical_floor_proposal']={'status':'proposal for review; existing thresholds unchanged','max_observed_source_or_ideal_output_rounding_residual_relative_db':floor,'proposed_residual_ceiling_relative_input_db':ceiling,'formula_absolute_rms':'input_rms * 10**(ceiling_db/20)','classification':'Mark a wanted-tone residual floor-limited when reference residual RMS is below this ceiling. Report raw dB deltas; propose testing absolute RMS against the ceiling there, rather than requiring 0.25 dB relative agreement. This proposal does not exempt wanted gain, total alias energy, or total image rejection from the 0.25 dB comparison.','controls_are_not_proof':'Ideal float rounding controls do not bound arbitrary filter coefficient error or all FFT sizes. Review this candidate ceiling before adopting it; the direct-DFT control covers one small rational transform, not every rate and FFT size.'}
    for name in ['default','regressed','reconstructed']:
        lib=a.root/name/'libfaar.so'
        for ir,orate in [(96000,44100),(48000,44100),(44100,48000),(192000,44100)]:
            cmd=[str(a.root/'memory'),str(lib),str(ir),str(orate),'2','3'];r=subprocess.run(cmd,check=True,capture_output=True,text=True)
            report['footprint'].append({'candidate':name,'in_rate':ir,'out_rate':orate,'command':cmd,'streaming_accounting':json.loads(r.stdout)})
    libraries=[a.root/x/'libfaar.so' for x in ['default','regressed','reconstructed']]+[a.root/'sox-instrumented-build/src/liblibsox.so',a.root/'kbsrc-cpu-build/libkbsrc.a',a.root/'ardftsrc']
    report['binary_sizes']=[{'path':str(x),'sha256':sha(x),'file_bytes':x.stat().st_size,'size_output':subprocess.check_output(['size','-B',str(x)],text=True)} for x in libraries]
    report['footprint_scope']='FAAR native helper excludes fixed client buffers from creation/retained deltas and includes arena+mmap allocations. Retained high water is sampled after calls, so it excludes freed intra-call temporaries. Massif separately reports measured peak heap including driver buffers; reference whole-file RSS/heap is not labeled streaming memory. Binary references include unrelated application/library code.'
    save(a.root/'validation.json',report);print(a.root/'validation.json')

if __name__=='__main__':main()
