#!/usr/bin/env python3
"""Independent 70-digit coefficient and selected impulse-response controls.

Decimal Taylor series implement sin and I0 independently of the C/libm path.
SoX exports are numeric observations, not imported implementation code.
"""
import argparse
from decimal import Decimal as D, localcontext
from functools import lru_cache
import json
import math
from pathlib import Path
import re
import struct
import subprocess

import numpy as np
PI=D('3.141592653589793238462643383279502884197169399375105820974944592307816406')

def sin(x):
    x=(x+PI)%(2*PI)-PI
    if x < -PI:x+=2*PI
    total=term=x
    for k in range(1,400):
        term *= -x*x/D(2*k*(2*k+1));total+=term
        if abs(term)<D('1e-68'):break
    return total

def i0(x):
    total=term=D(1)
    for k in range(1,400):
        term*=x*x/D(4*k*k);total+=term
        if abs(term)<abs(total)*D('1e-68'):break
    return total

@lru_cache(None)
def kaiser_sinc(n, cutoff, beta, center, radius, gain=D(1)):
    x=D(n)-center;u=x/radius
    if abs(u)>1:return D(0)
    sinc=2*cutoff if not x else sin(2*PI*cutoff*x)/(PI*x)
    return gain*sinc*i0(beta*(1-u*u).sqrt())/i0(beta)

@lru_cache(None)
def faar_prefilter(ir,orate):
    taps=[kaiser_sinc(j,D('0.4794')*D(orate)/D(ir),D(18),D(300),D(300)) for j in range(601)]
    scale=sum(taps);return [x/scale for x in taps]

@lru_cache(None)
def faar_phase(phi,length=127,cutoff=D(".49")):
    taps=[];center=D(length-1)/2
    for j in range(length):
        # The FAAR window is fixed to integer taps; the sinc alone is shifted.
        x=D(j)-center-phi
        sinc=2*cutoff if not x else sin(2*PI*cutoff*x)/(PI*x)
        u=(D(j)-center)/center
        window=D(0) if abs(u)>=1 else i0(D(38)*(1-u*u).sqrt())/i0(D(38))
        taps.append(sinc*window)
    scale=sum(taps);return [x/scale for x in taps]

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--root',type=Path,default=Path('/tmp/faar-recovery'));a=p.parse_args()
    work=a.root/'oracle';work.mkdir(exist_ok=True);report={'decimal_digits':70,'cases':[]}
    import hashlib
    report['oracle_source_sha256']=hashlib.sha256(Path(__file__).read_bytes()).hexdigest();report['driver_sha256']=hashlib.sha256((a.root/'driver').read_bytes()).hexdigest()
    with localcontext() as context:
        context.prec=70
        for ir,orate,variant in [(ir,orate,v) for v in ['default','wide'] for ir,orate in [(96000,44100),(48000,44100),(44100,48000)]]:
            n=8192;impulse=4096
            source=np.zeros(n,np.float32);source[impulse]=.5;source.tofile(work/'input.f32')
            dump=work/f'poly-{variant}-{ir}.bin';dft=work/f'dft-{variant}-{ir}.txt'
            import os
            env=dict(os.environ,FAAR_RECOVERY_DEBUG='1',FAAR_RECOVERY_COEFFICIENTS=str(dump),SOX_DSD_COEFFICIENTS=str(dft))
            cmd=[str(a.root/'driver'),'sox',variant,str(work/'input.f32'),str(work/'sox.f32'),str(ir),str(orate),'1','4096','65536']
            r=subprocess.run(cmd,env=env,check=True,capture_output=True,text=True);(work/f'sox-{variant}-{ir}.log').write_text(r.stderr)
            raw=dump.read_bytes();length,taps,phases=struct.unpack('3i',raw[:12]);prototype=np.frombuffer(raw,dtype='d',count=length,offset=12);bank=np.frombuffer(raw,dtype='d',offset=12+8*length).reshape(phases,taps)
            params=re.findall(r'make_lpf\(n=(\d+) Fc=([^ ]+) β=([^ ]+) ρ=([^ ]+) dc-norm=(\d+) scale=([^\)]+)',r.stderr)
            prototype_controls=[]
            for size,fc,beta,rho,dc,gain in params:
                size=int(size);center=D(size-1)/2;radius=center+D(rho)
                expected=np.array([float(kaiser_sinc(j,D(fc)/2,D(beta),center,radius,D(gain))) for j in range(size)])
                actual=prototype if size==length else np.loadtxt(dft)
                prototype_controls.append({'length':size,'cutoff_nyquist_normalized':float(fc),'beta':float(beta),'radius_extension':float(rho),'scale':float(gain),'max_absolute_error_vs_decimal':float(np.max(abs(expected-actual)))})
            indices=(taps-1-np.arange(taps)[None,:])*phases+np.arange(phases)[:,None]-1
            want=np.where(indices>=0,prototype[np.clip(indices,0,length-1)],0)
            mapping_error=float(np.max(abs(want-bank)))
            actual=np.fromfile(work/'sox.f32',dtype=np.float32)
            first_rate=96000 if orate==44100 else 88200;L=first_rate//ir
            pre=np.loadtxt(dft).astype(np.longdouble)
            # Centered convolution at the intermediate rate, then rational P/Q sampling.
            # The interior impulse avoids endpoint-policy ambiguity.
            P,Q=(147,320) if orate==44100 else (80,147)
            origin=P//2 if taps%2 else 0;preload=(taps-1)//2;precenter=(len(pre)-1)//2
            outputs=sorted(set([round(impulse*orate/ir)+i for i in range(-35,36)]+[P*(round(impulse*orate/ir)//P)]))
            errs=[]
            for m in outputs:
                base,phase=divmod(m*Q+origin,P)
                value=np.longdouble(0)
                for j in range(taps):
                    index=base+j-preload-L*impulse+precenter
                    if 0<=index<len(pre):value+=np.longdouble(bank[phase,j])*pre[index]*L*np.longdouble(.5)
                errs.append(abs(float(actual[m])-float(np.float32(value))))
            phase_responses=[]
            for hz in [1000,20000,21800,40000]:
                phase=np.arange(phases)[:,None]/phases
                response=(bank*np.exp(2j*np.pi*hz/first_rate*(np.arange(taps)[None,:]-(taps/2-1)-phase))).sum(1)
                phase_responses.append({'frequency_hz':hz,'mean_gain_db':20*math.log10(abs(response.mean())),'phase_modulation_rms':float(np.sqrt(np.mean(abs(response-response.mean())**2)))})
            # Independent FAAR cascade calculation includes its float32 stage handoff.
            cmd=[str(a.root/'driver'),'faar',str(a.root/'reconstructed/libfaar.so'),str(work/'input.f32'),str(work/'faar.f32'),str(ir),str(orate),'1','17','19']
            subprocess.run(cmd,check=True,capture_output=True,text=True)
            actual_faar=np.fromfile(work/'faar.f32',dtype=np.float32)
            pref=faar_prefilter(ir,orate) if orate==44100 else None;faarerrs=[];fractional=[]
            for m in outputs:
                numerator=m*ir;base,rem=divmod(numerator,orate);phi=D(rem)/D(orate);reverse=phi>D('.5')
                if reverse:base+=1;phi=1-phi
                y=D(0)
                if orate==44100:
                    coeff=faar_phase(phi)
                    if reverse:coeff=list(reversed(coeff))
                    for j in range(127):
                        q=base+j-63;idx=q-impulse+300
                        if 0<=idx<601 and 0<=q<n:
                            intermediate=np.float32(float(pref[idx]*D('.5')))
                            y+=D.from_float(float(intermediate))*coeff[j]
                else:
                    # Generic FAAR: 64-phase four-row Lagrange interpolation.
                    position=phi*64;p=min(int(position),31);delta=position-p
                    weights=[-delta*(delta-1)*(delta-2)/6,(delta+1)*(delta-1)*(delta-2)/2,-(delta+1)*delta*(delta-2)/2,(delta+1)*delta*(delta-1)/6]
                    j=impulse-base+255
                    if reverse:j=510-j
                    if 0<=j<511:
                        y=D('.5')*sum(w*faar_phase(D(p+offset)/64,511,D('.48'))[j] for offset,w in zip([-1,0,1,2],weights))
                faarerrs.append(abs(float(actual_faar[m])-float(np.float32(float(y)))))
                fractional.append(str(D(rem)/D(orate)))
            report['cases'].append({'sox_variant':variant,'in_rate':ir,'out_rate':orate,'sox_intermediate_rate':first_rate,'sox_phase_count':phases,'sox_initial_phase':origin,'sox_preload':preload,'sox_taps_per_phase':taps,'prototype_controls':prototype_controls,'deinterleave_mapping_max_absolute_error':mapping_error,'sox_interior_impulse_samples_checked':len(outputs),'sox_interior_impulse_max_abs_error':max(errs),'faar_interior_impulse_samples_checked':len(outputs),'faar_interior_impulse_max_abs_error':max(faarerrs),'fractional_positions':fractional,'phase_response':phase_responses})
    (a.root/'oracle.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
    if any(max(c['deinterleave_mapping_max_absolute_error'],c['sox_interior_impulse_max_abs_error'],c['faar_interior_impulse_max_abs_error'])>2e-8 or any(x['max_absolute_error_vs_decimal']>2e-13 for x in c['prototype_controls']) for c in report['cases']):raise SystemExit('independent oracle mismatch')

if __name__=='__main__':main()
