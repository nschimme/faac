#!/usr/bin/env python3
"""Serial, processing-only Callgrind profiles and separate full-process Massif data."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import numpy as np

PRIMARY=[(96000,44100),(48000,44100),(44100,48000)]

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--root',type=Path,default=Path('/tmp/faar-recovery'));p.add_argument('--method',choices=['callgrind','massif'],default='callgrind');p.add_argument('--seconds',type=int,default=2);a=p.parse_args()
    folder=a.root/'profiles';folder.mkdir(exist_ok=True);rows=[]
    engines=['faar-default','faar-regressed','faar-reconstructed','sox','sox-wide','kbsrc-fast-generic','kbsrc-fast-auto','ardftsrc']
    for ir,orate in PRIMARY:
        t=np.arange(ir*a.seconds)/ir;source=np.column_stack([.25*np.sin(2*np.pi*997*t),.2*np.sin(2*np.pi*10000*t)]).astype(np.float32)
        raw=folder/'input.f32';source.tofile(raw)
        from sys import path
        path.insert(0,str(Path(__file__).resolve().parents[1]))
        from compare_faar_ardft import write_float_wav
        # ARDFTSRC WAV helper is mono; use a stereo header directly.
        import struct
        payload=source.astype('<f4').tobytes();fmt=struct.pack('<HHIIHH',3,2,ir,ir*8,8,32);chunks=b'fmt '+struct.pack('<I',16)+fmt+b'data'+struct.pack('<I',len(payload))+payload
        wav=folder/'input.wav';wav.write_bytes(b'RIFF'+struct.pack('<I',len(chunks)+4)+b'WAVE'+chunks)
        for engine in engines:
            output=folder/f'{a.method}-{engine}-{ir}-{orate}.out';outpcm=folder/'output.f32'
            if engine=='ardftsrc':cmd=[str(a.root/'ardftsrc'),str(wav),str(folder/'output.wav'),str(orate),'32','16384','0.99']
            else:
                if engine.startswith('faar-'):kind='faar';lib=str(a.root/engine[5:]/'libfaar.so')
                elif engine.startswith('sox'):kind='sox';lib='wide' if engine=='sox-wide' else 'default'
                else:kind='kbsrc-fast';lib='generic' if engine.endswith('generic') else 'auto'
                cmd=[str(a.root/'driver'),kind,lib,str(raw),str(outpcm),str(ir),str(orate),'2','4096','65536']
            if a.method=='callgrind':prefix=['valgrind','--tool=callgrind','--instr-atstart=no','--callgrind-out-file='+str(output)]
            else:prefix=['valgrind','--tool=massif','--time-unit=B','--massif-out-file='+str(output)]
            env=dict(os.environ,FAAR_RECOVERY_PROFILE='1')
            r=subprocess.run(prefix+cmd,env=env,capture_output=True,text=True,check=True)
            row={'engine':engine,'in_rate':ir,'out_rate':orate,'seconds':a.seconds,'command':prefix+cmd,'input_sha256':hashlib.sha256(payload).hexdigest(),'driver_sha256':hashlib.sha256((a.root/'driver').read_bytes()).hexdigest(),'stdout':r.stdout,'stderr':r.stderr}
            contents=output.read_text()
            if a.method=='callgrind':
                row['instructions']=int(re.search(r'totals: (\d+)',contents).group(1))
                if row['instructions'] <= 0:raise RuntimeError('empty processing profile')
                annotation=subprocess.check_output(['callgrind_annotate','--auto=no','--inclusive=no','--threshold=99',str(output)],text=True)
                output.with_suffix('.txt').write_text(annotation);row['annotation']=annotation
            else:
                snapshots=[]
                for snapshot in contents.split('snapshot=')[1:]:
                    values={k:int(v) for k,v in re.findall(r'(mem_\w+_B)=(\d+)',snapshot)}
                    snapshots.append(values)
                row['peak_heap_and_overhead_bytes']=max(x.get('mem_heap_B',0)+x.get('mem_heap_extra_B',0) for x in snapshots)
                row['scope']='whole native driver: includes loaded input, result reservation, external engine setup and processing; not a streaming working-set number'
            rows.append(row);(a.root/(a.method+'.json')).write_text(json.dumps(rows,indent=2)+'\n');print(a.method,engine,ir,orate,flush=True)

if __name__=='__main__':main()
