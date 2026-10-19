#!/usr/bin/env python3
"""Bounded reference-setting sweep; derive kernel lengths from ARDFTSRC bandwidth."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import statistics
import subprocess
import sys
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from compare_faar_ardft import measure
from validate import wav_write

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--root',type=Path,default=Path('/tmp/faar-recovery'));a=p.parse_args();work=a.root/'matched-kbsrc';work.mkdir(exist_ok=True)
    prior=json.loads((a.root/'signals.json').read_text());refs={(x['in_rate'],x['out_rate'],x['frequency_hz'],x['peak']):x for x in prior['probes'] if x['engine']=='ardftsrc'}
    configs={str(beta):{'beta':beta,'length':math.sqrt(beta*beta+math.pi**2)/(math.pi*220.5)} for beta in [16,20,24]}
    report={'derivation':'For lower Nyquist 22050 Hz and bandwidth 0.99, transition width is 220.5 Hz. KBSRC length = sqrt(beta**2+pi**2)/(pi*220.5). Only beta 16, 20, 24 tested; no FAAR algorithm change.','source_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'sweep_driver_sha256':hashlib.sha256((a.root/'kbsrc-sweep').read_bytes()).hexdigest(),'configs':configs,'quality':[],'cases':[]}
    def kb(key,source,ir,orate):
        source.astype('<f4').tofile(work/'input.f32');ch=source.shape[1] if source.ndim==2 else 1;config=configs[key]
        cmd=[str(a.root/'kbsrc-sweep'),'generic',str(config['length']),str(config['beta']),str(work/'input.f32'),str(work/'output.f32'),str(ir),str(orate),str(ch)]
        r=subprocess.run(cmd,check=True,capture_output=True,text=True);stats=json.loads(r.stdout);stats['command']=cmd
        out=np.fromfile(work/'output.f32',dtype='<f4').reshape(-1,ch);stats['output_sha256']=hashlib.sha256(out.tobytes()).hexdigest();return out,stats
    for ir,orate in [(96000,44100),(48000,44100),(44100,48000)]:
        for f in ([1000,20000,21800,22060,23000] if ir>orate else [1000,20000,21800]):
            for peak in [.5,2.**-20]:
                x=(peak*np.sin(2*np.pi*f*np.arange(3*ir)/ir)).astype(np.float32)
                for key in configs:
                    out,stats=kb(key,x,ir,orate);metrics=measure(out[:,0].astype(float),x,ir,orate,f,peak)
                    ref=refs.get((ir,orate,f,peak))
                    row={'in_rate':ir,'out_rate':orate,'frequency_hz':f,'peak':peak,'config':key,'metrics':metrics,'reference':ref['metrics'] if ref else None,'timing':stats}
                    if ref:row['absolute_delta_vs_ardft_db']={k:abs(v-ref['metrics'][k]) if v is not None and ref['metrics'][k] is not None else None for k,v in metrics.items()}
                    report['quality'].append(row)
        # Same logical workload + one common reference input block as the main benchmark.
        g=math.gcd(ir,orate);factor=2*math.ceil(16384/(2*(orate//g)));block=ir//g*factor;logical=math.ceil(30*ir/block)*block;n=logical+block
        t=np.arange(logical)/ir;rng=np.random.default_rng(20261018);source=np.zeros((n,2),np.float32)
        for c in range(2):source[:logical,c]=.2*np.sin(2*np.pi*997*t+c*.1)+.17*np.sin(2*np.pi*10000*t)+.13*np.sin(2*np.pi*20000*t)+rng.uniform(-.02,.02,logical)
        case={'in_rate':ir,'out_rate':orate,'input_frames':n,'logical_frames':logical,'common_tail_frames':block,'input_sha256':hashlib.sha256(source.tobytes()).hexdigest(),'runs':{},'medians':{}}
        def run(name):
            if name in configs:return kb(name,source,ir,orate)[1]
            if name=='sox-wide':
                source.tofile(work/'input.f32');cmd=[str(a.root/'driver'),'sox','wide',str(work/'input.f32'),str(work/'output.f32'),str(ir),str(orate),'2','65536','65536']
                r=subprocess.run(cmd,check=True,capture_output=True,text=True);stats=json.loads(r.stdout);stats['command']=cmd;stats['output_sha256']=hashlib.sha256((work/'output.f32').read_bytes()).hexdigest();return stats
            wav_write(work/'input.wav',source,ir);cmd=[str(a.root/'ardftsrc'),str(work/'input.wav'),str(work/'reference.wav'),str(orate),'32','16384','0.99'];r=subprocess.run(cmd,check=True,capture_output=True,text=True)
            stats={k:float(v) for k,v in re.findall(r'(\w+)=([^ ]+)',re.search(r'ARDFTSRC_BENCH (.*)',r.stderr).group(1))};stats['command']=cmd;stats['output_sha256']=hashlib.sha256((work/'reference.wav').read_bytes()).hexdigest();return stats
        names=list(configs)+['sox-wide','ardftsrc']
        for name in names:run(name)
        for index in range(7):
            order=names[index%len(names):]+names[:index%len(names)]
            for name in order:case['runs'].setdefault(name,[]).append(run(name))
        for name,runs in case['runs'].items():case['medians'][name]={key:statistics.median(r[key] for r in runs) for key in ['setup_cpu_seconds','processing_cpu_seconds']}
        report['cases'].append(case);(a.root/'matched-kbsrc.json').write_text(json.dumps(report,indent=2)+'\n');print(ir,orate,case['medians'],flush=True)
    (a.root/'matched-kbsrc.json').write_text(json.dumps(report,indent=2)+'\n')

if __name__=='__main__':main()
