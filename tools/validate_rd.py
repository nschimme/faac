#!/usr/bin/env python3
"""Probe-off ADTS identity, deterministic active output, decode and cap smoke.

Uses short synthetic edge inputs so it can also run under sanitizers. This is
correctness validation, not a quality benchmark. Results include all commands.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import wave

def run(command, env=None):
    result = subprocess.run(list(map(str, command)), env=env, capture_output=True, text=True)
    if result.returncode or 'err 0x' in result.stderr: raise RuntimeError(f'{command}: {result.stderr[-2000:]}')
    return result

def digest(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('baseline'); ap.add_argument('probe'); ap.add_argument('fdkdec'); ap.add_argument('out')
    a = ap.parse_args(); out = Path(a.out); out.mkdir(parents=True, exist_ok=True)
    inputs = []
    for sr in [44100,48000]:
        for kind in ['silence','impulses','tone','sparse']:
            path = out/f'{kind}-{sr}.wav'
            samples = []
            for i in range(sr//4):
                if kind == 'silence': left = right = 0
                elif kind == 'impulses': left = 32760 if i%997 == 0 else 0; right = -left
                elif kind == 'tone': left = int(24000*math.sin(i*2*math.pi*997/sr)); right = int(left*0.93)
                else:
                    left = int(16000*math.sin(i*2*math.pi*3000/sr)) if i%3072 < 512 else 0
                    right = int(16000*math.sin(i*2*math.pi*700/sr)) if i%2048 < 128 else 0
                samples.extend([left,right])
            with wave.open(str(path),'wb') as w:
                w.setparams((2,2,sr,0,'NONE','not compressed')); w.writeframes(struct.pack('<'+'h'*len(samples),*samples))
            inputs.append(path)
    records = []
    for clip in inputs:
        for profile,rate in [('lc',96),('he-aac-v1',48)]:
            for mode in [[],['--shortctl','1'],['--shortctl','2'],['-q','5000','--no-pns'],['-q','5000','--cap-rate','48'],['--cbr']]:
                args = ['--overwrite','--object-type',profile,*([] if '-q' in mode else ['-b',str(rate)]),*mode]
                stem = f'{clip.stem}-{profile}-{len(records)}'
                paths = [out/(stem+f'-{i}.aac') for i in range(4)]
                env = dict(os.environ); env.pop('FAAC_RD_LAMBDA',None)
                for binary,path in [(a.baseline,paths[0]),(a.probe,paths[1])]:
                    run([binary,*args,'-o',path,clip],env)
                assert paths[0].read_bytes() == paths[1].read_bytes(), ('probe-off mismatch',clip,args)
                env['FAAC_RD_LAMBDA'] = '0.1'; env['FAAC_RD_TRACE'] = '1'
                active = run([a.probe,*args,'-o',paths[2],clip],env)
                run([a.probe,*args,'-o',paths[3],clip],env)
                assert paths[2].read_bytes() == paths[3].read_bytes(), ('nondeterministic',clip,args)
                run(['ffmpeg','-v','error','-xerror','-y','-i',paths[2],'-f','null','-'])
                mp4 = out/'fdk-input.m4a'
                run([a.probe,*args,'-o',mp4,clip],env)
                run([a.fdkdec,mp4,out/'decoded.wav'])
                info=json.loads(run(['ffprobe','-v','error','-show_entries','stream=profile','-of','json',paths[2]]).stdout)
                assert info['streams'][0]['profile'] == ('LC' if profile == 'lc' else 'HE-AAC')
                records.append(dict(clip=str(clip),args=args,off_sha=digest(paths[0]),active_sha=digest(paths[2]),trace=active.stderr))
                print(len(records),clip.name,profile,mode,'OK',flush=True)
    (out/'validation.json').write_text(json.dumps(records,indent=2)+'\n')

if __name__ == '__main__': main()
