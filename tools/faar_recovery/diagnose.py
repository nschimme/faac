#!/usr/bin/env python3
"""Shared resampler diagnostics. No composite quality score or automatic promotion."""
import argparse
import hashlib
import json
import math
import os
import platform
import re
import resource
import statistics
import subprocess
import sys
import time
from pathlib import Path

import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from compare_faar_ardft import write_float_wav, read_float_wav, measure

PRIMARY=[(96000,44100),(48000,44100),(44100,48000)]
EXTRA=[(96000,48000),(192000,44100),(48000,48000),(44100,44101)]
ENGINES=['faar-default','faar-regressed','faar-reconstructed','faar-candidate','sox','sox-wide','sox-steep','sox-extreme','kbsrc-fast-generic','kbsrc-fast-auto','kbsrc-exact-generic','kbsrc-exact-auto','ardftsrc']

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def db(x):return 20*math.log10(max(float(x),1e-300))
def save(path,data):path.write_text(json.dumps(data,indent=2,sort_keys=True,allow_nan=False)+'\n')

def tone_amplitude(samples,frequency,rate):
    """Least-squares sinusoid amplitude, with DC removed, at an arbitrary Hz."""
    y=np.asarray(samples,dtype=np.float64)
    t=np.arange(len(y),dtype=np.float64)/rate
    phase=2*np.pi*frequency*t
    design=np.column_stack((np.sin(phase),np.cos(phase),np.ones(len(y))))
    coeff,_,_,_=np.linalg.lstsq(design,y,rcond=None)
    return float(np.hypot(coeff[0],coeff[1]))

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root',type=Path,default=Path('/tmp/faar-recovery'))
    p.add_argument('--reference',type=Path)
    p.add_argument('--phase',choices=['baseline','signals','bench','all'],default='all')
    p.add_argument('--engines',nargs='+',choices=ENGINES,default=ENGINES)
    p.add_argument('--candidate-library',type=Path)
    p.add_argument('--faar-quality',type=int,choices=(0,1,2),
                   help='FAAR preset passed to the diagnostic driver (0=Fast, 1=Good, 2=High)')
    p.add_argument('--bench-extra',action='store_true',
                   help='include the diagnostic EXTRA conversion set in benchmark runs')
    p.add_argument('--bench-pair',action='append',nargs=2,type=int,metavar=('IN_RATE','OUT_RATE'),
                   help='add an explicit conversion pair to benchmark runs; may be repeated')
    p.add_argument('--signal-pair',action='append',nargs=2,type=int,metavar=('IN_RATE','OUT_RATE'),
                   help='run signal diagnostics for explicit conversion pairs; may be repeated')
    p.add_argument('--signal-matrix',action='store_true',
                   help='run the 357 supported-rate and +/-1 Hz neighbor pairs from FAAR fixtures')
    p.add_argument('--image-matrix',action='store_true',
                   help='measure in-band images for supported upsampling pairs')
    p.add_argument('--transient-matrix',action='store_true',
                   help='measure central impulse symmetry and endpoint response for the 357-pair rate matrix')
    p.add_argument('--image-pair',action='append',nargs=2,type=int,metavar=('IN_RATE','OUT_RATE'),
                   help='measure in-band images for an explicit upsampling pair; may be repeated')
    p.add_argument('--tones-only',action='store_true',
                   help='skip impulse, two-tone, and noise cases during response sweeps')
    p.add_argument('--suffix',default='')
    p.add_argument('--repeats',type=int,default=7)
    p.add_argument('--seconds',type=int,default=30)
    a=p.parse_args()
    if sum((bool(a.signal_matrix),bool(a.image_matrix),bool(a.image_pair),bool(a.signal_pair),bool(a.transient_matrix)))>1:
        p.error('--signal-matrix, --image-matrix, --image-pair, --signal-pair, and --transient-matrix cannot be combined')
    a.reference=a.reference or a.root/'ardftsrc'; work=a.root/'runs';work.mkdir(exist_ok=True)
    if 'faar-candidate' in a.engines and not a.candidate_library:
        p.error('--candidate-library is required with faar-candidate')
    manifest=json.loads((a.root/'build-manifest.json').read_text())
    metadata={'platform':platform.platform(),'cpuinfo':next(x.split(':',1)[1].strip() for x in Path('/proc/cpuinfo').read_text().splitlines() if x.startswith('model name')),'numpy':np.__version__,'build_manifest_sha256':sha(a.root/'build-manifest.json'),'reference_sha256':sha(a.reference),'driver_sha256':sha(a.root/'driver'),'candidate_library_sha256':sha(a.candidate_library) if a.candidate_library else None,'faar_quality':a.faar_quality,'thread_policy':'one processing thread; KBSRC coefficient generation internally spawns workers even with threads=1','timing':'Native process CPU; driver I/O and source conversion excluded. FAAR collection/flush and SoX chain scheduler included; KBSRC processing includes run_engine FFT planning/scratch allocation, collection excluded; ARDFTSRC uses its existing instrumented boundaries. KBSRC scratch copy changes only the existing clock to process CPU time.','quality_gate':'0.25 dB response tolerance; wanted-tone residual floor only, as calibrated in the local FAAR controls'}
    def convert(engine,source,ir,orate,*,block=4096,capacity=65536,pad_reference=True,debug=False):
        source=np.asarray(source,dtype='<f4');channels=source.shape[1] if source.ndim==2 else 1
        frames=len(source);infile=work/'input.f32';outfile=work/'output.f32';source.tofile(infile)
        if engine=='ardftsrc':
            g=math.gcd(ir,orate);factor=2*math.ceil(16384/(2*(orate//g)));tail=ir//g*factor;delay=orate//g*factor//2
            data=np.concatenate([source,np.zeros((tail,channels) if channels>1 else tail,dtype=np.float32)]) if pad_reference else source
            # The reference's AudioFile loader supports IEEE mono/stereo float WAV.
            raw=np.asarray(data,dtype='<f4').tobytes()
            import struct
            fmt=struct.pack('<HHIIHH',3,channels,ir,ir*channels*4,channels*4,32)
            chunks=b'fmt '+struct.pack('<I',len(fmt))+fmt+b'data'+struct.pack('<I',len(raw))+raw
            wav=work/'input.wav';wav.write_bytes(b'RIFF'+struct.pack('<I',len(chunks)+4)+b'WAVE'+chunks)
            cmd=[str(a.reference),str(wav),str(work/'reference.wav'),str(orate),'32','16384','0.99']
        else:
            if engine=='faar-candidate':kind='faar';variant=str(a.candidate_library)
            elif engine.startswith('faar-'):kind='faar';variant=str(a.root/engine[5:]/'libfaar.so')
            elif engine.startswith('sox'):kind='sox';variant={'sox':'default','sox-wide':'wide','sox-steep':'steep','sox-extreme':'extreme'}[engine]
            else:
                kind='kbsrc-exact' if 'exact' in engine else 'kbsrc-fast';variant='generic' if engine.endswith('generic') else 'auto'
            cmd=[str(a.root/'driver'),kind,variant,str(infile),str(outfile),str(ir),str(orate),str(channels),str(block),str(capacity)]
            if kind=='faar' and a.faar_quality is not None:
                cmd.append(str(a.faar_quality))
        env=dict(os.environ)
        if debug:
            env['FAAR_RECOVERY_DEBUG']='1';env['FAAR_RECOVERY_COEFFICIENTS']=str(work/f'coefficients-{ir}-{orate}.bin');env['SOX_DSD_COEFFICIENTS']=str(work/f'dft-{ir}-{orate}.txt')
        start=time.monotonic();before=resource.getrusage(resource.RUSAGE_CHILDREN)
        run=subprocess.run(cmd,env=env,check=True,capture_output=True,text=True)
        after=resource.getrusage(resource.RUSAGE_CHILDREN)
        timing={'command':cmd,'child_cpu_including_io':after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime,'wall_including_io':time.monotonic()-start}
        if engine=='ardftsrc':
            raw=(work/'reference.wav').read_bytes(); pos=12; out=None
            import struct
            while pos+8<=len(raw):
                tag=raw[pos:pos+4];n=struct.unpack_from('<I',raw,pos+4)[0]
                if tag==b'data':out=np.frombuffer(raw,dtype='<f4',count=n//4,offset=pos+8).copy().reshape(-1,channels);break
                pos+=8+n+(n&1)
            if out is None:raise RuntimeError('reference WAV missing data')
            row=re.search(r'ARDFTSRC_BENCH (.*)',run.stderr)
            if not row:raise RuntimeError('reference timing missing')
            for field in row.group(1).split():
                key,value=field.split('=',1);timing[key]=float(value)
            timing['raw_output_frames']=len(out);timing['alignment_delay_frames']=delay if pad_reference else 0;timing['tail_input_frames']=tail if pad_reference else 0
            if pad_reference:out=out[delay:delay+(frames*orate+ir-1)//ir]
        else:
            timing.update(json.loads(run.stdout));out=np.fromfile(outfile,dtype='<f4').reshape(-1,channels)
            if engine.startswith('faar-'):timing['library_sha256']=sha(variant)
        if not np.isfinite(out).all():raise RuntimeError('nonfinite output')
        timing['frames']=len(out);timing['output_sha256']=hashlib.sha256(out.tobytes()).hexdigest()
        if debug:(work/f'sox-{ir}-{orate}.log').write_text(run.stderr)
        return out,timing

    if a.phase in ['baseline','all']:
        report={'metadata':metadata,'tests':[],'capabilities':{'FAAR':'native streaming API; ceil counts, ZERO/HOLD boundary, dither; no public reset API','SoX':'native effect chain; partial input consumption, flow/drain; nearest-frame counts; no FAAR endpoint/dither contract','KBSRC':'whole-file API; ceil counts; zero or LPC endpoints; chunk/capacity/flush invariance unsupported','ARDFTSRC':'block/whole-file CLI; block padding and explicit alignment required; not FAAR streaming API'}}
        for name in ['default','regressed','reconstructed']:
            for test in ['quality','tuneout_matrix']:
                cmd=[str(a.root/name/test)];r=subprocess.run(cmd,capture_output=True,text=True)
                report['tests'].append({'candidate':name,'test':test,'command':cmd,'exit':r.returncode,'stdout':r.stdout,'stderr':r.stderr})
                print(name,test,r.returncode,flush=True)
        # Native adapters must show their own contracts rather than receive simulated passes.
        report['short_streams']=[]
        for engine in a.engines:
            if engine=='ardftsrc':continue
            for ir,orate in [(48000,44100),(48000,48000),(44100,44101)]:
                for n in [1,17,257]:
                    source=np.arange(n,dtype=np.float32)/(max(n,1)*8);source[0]=-0.0
                    output,t=convert(engine,source,ir,orate,block=17,capacity=19)
                    report['short_streams'].append({'engine':engine,'in_rate':ir,'out_rate':orate,'input_frames':n,'ceil_frames':(n*orate+ir-1)//ir,'actual_frames':len(output),'equal_rate_bit_identity':output[:,0].tobytes()==source.tobytes() if ir==orate else None,'timing':t})
        report['native_block_checks']=[]
        report['channel_checks']=[]
        for engine in a.engines:
            if engine=='ardftsrc':continue
            mono=(.25*np.sin(np.arange(257)*.17)).astype(np.float32)
            if engine.startswith('faar-') or engine.startswith('sox'):
                large,_=convert(engine,mono,48000,44100,block=4096,capacity=65536)
                small,_=convert(engine,mono,48000,44100,block=17,capacity=19)
                report['native_block_checks'].append({'engine':engine,'max_abs_error':float(np.max(abs(large-small))) if large.shape==small.shape else None,'frame_count_match':large.shape==small.shape,'output_capacity_tested':engine.startswith('faar-'),'SoX_note':'SoX chain buffer size changes; no simulated FAAR flush or output-capacity contract'})
            for channels in [2,6,8]:
                source=np.column_stack([mono*(1+c/8) for c in range(channels)]).astype(np.float32)
                combined,_=convert(engine,source,48000,44100)
                err=0.
                for c in range(channels):
                    separate,_=convert(engine,source[:,c],48000,44100)
                    if len(separate)!=len(combined):raise RuntimeError('channel frame counts differ')
                    err=max(err,float(np.max(abs(separate[:,0]-combined[:,c]))))
                report['channel_checks'].append({'engine':engine,'channels':channels,'max_abs_error_vs_independent_mono':err})
        save(a.root/f'baseline{a.suffix}.json',report)

    if a.phase in ['signals','all']:
        report={'metadata':metadata,'probes':[],'floor_controls':[],'other_signals':[],
                'image_probes':[],'transient_probes':[],
                'tones_only':a.tones_only}
        if a.transient_matrix:
            rate_path=Path(__file__).resolve().parents[2]/'libfaar/verification/fixtures/rates.txt'
            rates=[int(x) for x in rate_path.read_text().split()]
            signal_pairs=[(ir,orate) for ir in rates for orate in rates]
            for rate in rates:
                signal_pairs.extend(((rate,rate-1),(rate-1,rate),
                                     (rate,rate+1),(rate+1,rate)))
            signal_pairs=sorted(set(signal_pairs))
            a.tones_only=True
            report['transient_matrix']={'expected_pairs':len(signal_pairs),'completed_pairs':0,
                'impulse_input_frames':'2 * input_rate','endpoint_window':'first/last 10 ms',
                'central_window':'+/-200 ms (clipped to stream)','faar_boundary':'ZERO (diagnostic driver)'}
        elif a.image_pair:
            signal_pairs=[tuple(x) for x in a.image_pair]
            if any(orate<=ir for ir,orate in signal_pairs):
                p.error('--image-pair requires output rate greater than input rate')
            a.image_matrix=True
            a.tones_only=True
        elif a.image_matrix:
            rate_path=Path(__file__).resolve().parents[2]/'libfaar/verification/fixtures/rates.txt'
            rates=[int(x) for x in rate_path.read_text().split()]
            signal_pairs=[(ir,orate) for ir in rates for orate in rates if orate>ir]
            for rate in rates:
                signal_pairs.extend(((rate,rate+1),(rate-1,rate),(rate-1,rate+1)))
            signal_pairs=sorted(set((ir,orate) for ir,orate in signal_pairs if orate>ir))
            a.tones_only=True
        elif a.signal_matrix:
            rate_path=Path(__file__).resolve().parents[2]/'libfaar/verification/fixtures/rates.txt'
            rates=[int(x) for x in rate_path.read_text().split()]
            signal_pairs=[(ir,orate) for ir in rates for orate in rates]
            for rate in rates:
                signal_pairs.extend(((rate,rate-1),(rate-1,rate),
                                     (rate,rate+1),(rate+1,rate)))
        else:
            signal_pairs=[tuple(x) for x in a.signal_pair] if a.signal_pair else PRIMARY+EXTRA
        for ir,orate in signal_pairs:
            if a.transient_matrix:
                input_frames=2*ir
                source=np.zeros(input_frames,dtype=np.float32)
                source[0]=0.25
                source[ir]=0.5
                source[-1]=-0.25
                for engine in a.engines:
                    output,t=convert(engine,source,ir,orate)
                    center=orate
                    radius=min(orate//5,center,len(output)-center-1)
                    response=output[center-radius:center+radius+1,0].astype(np.float64)
                    time_axis=np.arange(-radius,radius+1)
                    guard=max(1,orate//1000)
                    before=response[time_axis < -guard]
                    after=response[time_axis > guard]
                    before_energy=float(np.dot(before,before))
                    after_energy=float(np.dot(after,after))
                    edge_frames=max(1,min(orate//100, len(output)//2))
                    report['transient_probes'].append({'engine':engine,'in_rate':ir,'out_rate':orate,
                        'input_frames':input_frames,'expected_center_frame':center,
                        'window_radius_frames':radius,
                        'main_peak_offset_frames':int(np.argmax(abs(response))-radius),
                        'peak_amplitude':float(np.max(abs(response))),
                        'pre_ringing_energy':before_energy,'post_ringing_energy':after_energy,
                        'pre_ringing_to_post_db':db(before_energy/max(after_energy,1e-300)),
                        'time_reversal_error_relative':float(np.linalg.norm(response-response[::-1])/
                            max(np.linalg.norm(response),1e-300)),
                        'head_edge_energy':float(np.dot(output[:edge_frames,0],output[:edge_frames,0])),
                        'tail_edge_energy':float(np.dot(output[-edge_frames:,0],output[-edge_frames:,0])),
                        'edge_window_frames':edge_frames,
                        'output_sha256':hashlib.sha256(output.astype('<f4').tobytes()).hexdigest(),
                        'timing':t})
                report['transient_matrix']['completed_pairs']+=1
                if report['transient_matrix']['completed_pairs']%8==0:
                    save(a.root/f'signals{a.suffix}.json',report)
                    print('transient matrix complete',ir,orate,
                          report['transient_matrix']['completed_pairs'],flush=True)
                continue
            lower=min(ir,orate);nyq=lower/2
            alias_probes=[]
            if ir>orate:
                gap=(ir-orate)/2
                alias_probes=[nyq+min(100.0,gap/2),
                              min(ir/2-1.0,nyq+950.0), ir*.49]
                if gap>200.0: alias_probes.append(min(ir/2-100.0,nyq+950.0))
            frequencies=sorted(set([1000,min(10000,int(.2*lower)),min(20000,int(.49*lower)),int(.45*lower),int(.475*lower),int(.49*lower),min(21800,int(.495*lower)),int(.499*lower)]+alias_probes))
            if (ir,orate) in EXTRA and not a.image_matrix:frequencies=sorted(set([1000,int(.45*lower)]+([int(nyq+10)] if ir>orate else [])))
            probes=[(f,.5) for f in frequencies]
            if (ir,orate) in PRIMARY:probes += [(1000,2.**-20),(min(20000,int(.49*lower)),2.**-20),(1000,2.**-60)]
            for f,peak in probes:
                source=(peak*np.sin(2*np.pi*f*np.arange(3*ir)/ir)).astype(np.float32)
                ideal=(peak*np.sin(2*np.pi*f*np.arange(3*orate,dtype=np.longdouble)/orate)).astype(np.float32)
                floor=measure(ideal.astype(float),source,ir,orate,f,peak)
                report['floor_controls'].append({'in_rate':ir,'out_rate':orate,'frequency_hz':f,'peak':peak,'ideal_float32_rounding':floor,'input_rounding_relative_rms_db':db(np.sqrt(np.mean((source.astype(np.longdouble)-peak*np.sin(2*np.pi*f*np.arange(3*ir,dtype=np.longdouble)/ir))**2,dtype=np.longdouble))/(peak/math.sqrt(2)))})
                for engine in a.engines:
                    output,t=convert(engine,source,ir,orate)
                    m=measure(output[:,0].astype(float),source,ir,orate,f,peak)
                    report['probes'].append({'engine':engine,'in_rate':ir,'out_rate':orate,'frequency_hz':f,'peak':peak,'input_sha256':hashlib.sha256(source.tobytes()).hexdigest(),'metrics':m,'local_unity_gain_1e4_pass':abs(10**(m['wanted_gain_db']/20)-1)<=1e-4 if f in [1000,10000,20000] and f<nyq else None,'timing':t})
                    if a.image_matrix and f==int(.45*ir):
                        # Ignore the first and last half-second to keep endpoint
                        # extension out of this steady-state spectral estimate.
                        begin=min(orate//2,max(0,len(output)//2-orate//2))
                        end=min(len(output),begin+orate)
                        segment=output[begin:end,0]
                        image_freqs=[]
                        max_harmonic=int((orate/2+f)//ir)+1
                        for harmonic in range(1,max_harmonic+1):
                            for image_f in (harmonic*ir-f,harmonic*ir+f):
                                if 10.0<image_f<orate/2-10.0 and abs(image_f-f)>10.0:
                                    image_freqs.append(image_f)
                        wanted_amp=tone_amplitude(segment,f,orate)
                        images=[]
                        for image_f in sorted(set(image_freqs)):
                            image_amp=tone_amplitude(segment,image_f,orate)
                            images.append({'frequency_hz':image_f,'amplitude':image_amp,
                                'attenuation_db_vs_wanted':db(image_amp/max(wanted_amp,1e-300))})
                        report['image_probes'].append({'engine':engine,'in_rate':ir,'out_rate':orate,
                            'input_tone_hz':f,'input_rate_fraction':f/ir,
                            'wanted_amplitude':wanted_amp,'images':images,
                            'segment_frames':len(segment),'input_sha256':hashlib.sha256(source.tobytes()).hexdigest()})
            # Shared impulses, multitone/noise, and channel isolation use zero endpoints.
            n=ir; rng=np.random.default_rng(20261018)
            if not a.tones_only:
                signals={'impulses':np.zeros(n,np.float32),'two_tone':(.25*np.sin(2*np.pi*10000*np.arange(3*ir)/ir)+.25*np.sin(2*np.pi*12000*np.arange(3*ir)/ir)).astype(np.float32),'multitone_noise':(.15*np.sin(2*np.pi*997*np.arange(n)/ir)+.1*np.sin(2*np.pi*.4*lower*np.arange(n)/ir)+rng.uniform(-.01,.01,n)).astype(np.float32)}
                signals['impulses'][[0,n//2,n-1]]=[.25,.5,-.25]
                for label,source in signals.items():
                    for engine in a.engines:
                        output,t=convert(engine,source,ir,orate,debug=engine=='sox' and label=='impulses')
                        imd=None
                        if label=='two_tone':
                            y=output[orate:2*orate,0].astype(float);spectrum=np.fft.rfft(y)
                            imd={str(f):db(2*abs(spectrum[f])/len(y)) for f in [2000,8000,14000] if f<orate/2}
                        if label=='impulses':
                            center=int(round((n//2)*orate/ir))
                            radius=min(orate//5,center,len(output)-center-1)
                            response=output[center-radius:center+radius+1,0].astype(np.float64)
                            time_axis=np.arange(-radius,radius+1)
                            guard=max(1,orate//1000)
                            before=response[time_axis < -guard]
                            after=response[time_axis > guard]
                            before_energy=float(np.dot(before,before))
                            after_energy=float(np.dot(after,after))
                            reversal_error=float(np.linalg.norm(response-response[::-1])/
                                max(np.linalg.norm(response),1e-300))
                            report['transient_probes'].append({'engine':engine,'in_rate':ir,'out_rate':orate,
                                'expected_center_frame':center,'window_radius_frames':radius,
                                'main_peak_offset_frames':int(np.argmax(abs(response))-radius),
                                'peak_amplitude':float(np.max(abs(response))),
                                'pre_ringing_energy':before_energy,'post_ringing_energy':after_energy,
                                'pre_ringing_to_post_db':db(before_energy/max(after_energy,1e-300)),
                                'time_reversal_error_relative':reversal_error,
                                'response_sha256':hashlib.sha256(response.astype('<f4').tobytes()).hexdigest()})
                        report['other_signals'].append({'engine':engine,'signal':label,'imd_product_peak_dbfs':imd,'peak_index':int(np.argmax(abs(output[:,0]))),'in_rate':ir,'out_rate':orate,'input_sha256':hashlib.sha256(source.tobytes()).hexdigest(),'rms':float(np.sqrt(np.mean(output.astype(float)**2))),'peak':float(np.max(np.abs(output))),'head_energy':float(np.sum(output[:100].astype(float)**2)),'tail_energy':float(np.sum(output[-100:].astype(float)**2)),'timing':t})
            save(a.root/f'signals{a.suffix}.json',report);print('signals complete',ir,orate,flush=True)
        # Compare metrics only on the same probe; floor proximity is annotated, not excused.
        lookup={(x['in_rate'],x['out_rate'],x['frequency_hz'],x['peak']):x for x in report['probes'] if x['engine']=='ardftsrc'}
        for x in report['probes']:
            ref=lookup.get((x['in_rate'],x['out_rate'],x['frequency_hz'],x['peak']))
            if ref:
                x['absolute_delta_vs_ardft_db']={k:abs(v-ref['metrics'][k]) if v is not None and ref['metrics'][k] is not None else None for k,v in x['metrics'].items()}
                phase_delta=x['metrics']['wanted_phase_radians']-ref['metrics']['wanted_phase_radians']
                x['phase_delta_vs_ardft_radians']=math.atan2(math.sin(phase_delta),math.cos(phase_delta))
        save(a.root/f'signals{a.suffix}.json',report)

    if a.phase in ['bench','all']:
        report={'metadata':metadata,'repeats':a.repeats,'warmups':1,'cases':[]}
        bench_pairs=([tuple(x) for x in a.bench_pair] if a.bench_pair
                     else PRIMARY+(EXTRA if a.bench_extra else []))
        for ir,orate in bench_pairs:
            g=math.gcd(ir,orate);unit=ir//g;factor=2*math.ceil(16384/(2*(orate//g)));refblock=unit*factor
            logical=math.ceil(a.seconds*ir/refblock)*refblock
            n=logical+refblock;t=np.arange(logical)/ir;rng=np.random.default_rng(20261018)
            source=np.zeros((n,2),np.float32)
            for c in range(2):source[:logical,c]=(.2*np.sin(2*np.pi*997*t+c*.1)+.17*np.sin(2*np.pi*10000*t)+.13*np.sin(2*np.pi*20000*t)+rng.uniform(-.02,.02,logical))
            case={'in_rate':ir,'out_rate':orate,'logical_frames':logical,'common_tail_frames':refblock,'input_frames':n,'input_sha256':hashlib.sha256(source.tobytes()).hexdigest(),'runs':{},'medians':{}}
            for e in a.engines:convert(e,source,ir,orate,block=65536,pad_reference=False)
            for repeat in range(a.repeats):
                order=a.engines[repeat%len(a.engines):]+a.engines[:repeat%len(a.engines)]
                for e in order:
                    _,timing=convert(e,source,ir,orate,block=65536,pad_reference=False)
                    timing['round']=repeat;case['runs'].setdefault(e,[]).append(timing)
            for e,runs in case['runs'].items():
                keys=['setup_cpu_seconds','processing_cpu_seconds','api_total_cpu_seconds','setup_live_heap_bytes','whole_process_peak_rss_kib']
                case['medians'][e]={key:statistics.median([r[key] for r in runs]) for key in keys if key in runs[0]}
                case['medians'][e]['deterministic_output']=len({r['output_sha256'] for r in runs})==1
                case['medians'][e]['expected_count']=(n*orate+ir-1)//ir
                case['medians'][e]['actual_count']=runs[0]['frames']
            report['cases'].append(case);save(a.root/f'bench{a.suffix}.json',report);print('bench complete',ir,orate,flush=True)

if __name__=='__main__':main()
