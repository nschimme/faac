#!/usr/bin/env python3
import hashlib,json,math,os,struct,subprocess,wave
from pathlib import Path
root=Path('/private/tmp/faac-upstream098-smoke')
inputs=root/'inputs'; outdir=root/'outputs'; inputs.mkdir(parents=True,exist_ok=True); outdir.mkdir(parents=True,exist_ok=True)
encoders={'baseline':'/private/tmp/faac-upstream098-baseline/build-release-dec4/frontend/faac','fixed8':'/private/tmp/faac-upstream098-fixed8/build-release-dec4/frontend/faac'}
fdk='/private/tmp/faac-upstream098-fdk-new/fdkdec'
N=2049; rate=48000

def make(name,ch,kind):
 p=inputs/f'{name}-{ch}ch.wav'
 with wave.open(str(p),'wb') as w:
  w.setnchannels(ch);w.setsampwidth(2);w.setframerate(rate)
  b=bytearray()
  for i in range(N):
   if kind=='silence': vals=[0]*ch
   else:
    vals=[]
    for c in range(ch):
     v=0 if i in (0,N-1) else int(11000*math.sin(2*math.pi*(440+120*c)*i/rate))
     vals.append(v)
   b.extend(struct.pack('<'+'h'*ch,*vals))
  w.writeframes(b)
 return p

def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
rows=[]
for kind in ('tone','silence'):
 for ch in (1,2):
  inp=make(kind,ch,kind)
  # Forced LC: compare ADTS bytes between the clean base and fixed8 source.
  lc_paths={}
  for variant,exe in encoders.items():
   dest=outdir/f'{kind}-{ch}ch-{variant}-lc.aac'
   cmd=[exe,'-b','48','--cbr','--object-type','lc','-a','--overwrite','-o',str(dest),str(inp)]
   cp=subprocess.run(cmd,capture_output=True,text=True,check=False)
   if cp.returncode: raise RuntimeError(f'LC failed {variant}: {cp.stderr[-1000:]}')
   lc_paths[variant]=dest
  if sha(lc_paths['baseline'])!=sha(lc_paths['fixed8']): raise RuntimeError(f'LC bitstream identity failed: {kind}/{ch}')
  rows.append({'kind':kind,'channels':ch,'mode':'LC CBR 48 ADTS','input_frames':N,'input_sha256':sha(inp),'baseline_encoded_sha256':sha(lc_paths['baseline']),'fixed8_encoded_sha256':sha(lc_paths['fixed8']),'exact_bitstream_identity':True})
  # HE-v1 CBR partial final frame, FDK decode both source builds.
  for variant,exe in encoders.items():
   dest=outdir/f'{kind}-{ch}ch-{variant}-hev1.m4a'; dec=outdir/f'{kind}-{ch}ch-{variant}-hev1.wav'
   cmd=[exe,'-b','48','--cbr','--object-type','he-aac-v1','--overwrite','-o',str(dest),str(inp)]
   cp=subprocess.run(cmd,capture_output=True,text=True,check=False)
   if cp.returncode: raise RuntimeError(f'HE encode failed {variant}: {cp.stderr[-1000:]}')
   dc=subprocess.run([fdk,str(dest),str(dec)],capture_output=True,text=True,check=False)
   if dc.returncode: raise RuntimeError(f'FDK decode failed {variant}: {dc.stderr[-1000:]}')
   with wave.open(str(dec),'rb') as w:
    info={'channels':w.getnchannels(),'rate':w.getframerate(),'frames':w.getnframes(),'sample_width':w.getsampwidth()}
   rows.append({'kind':kind,'channels':ch,'mode':'HE-AAC v1 CBR 48','variant':variant,'input_frames':N,'input_sha256':sha(inp),'encoded_sha256':sha(dest),'decoded_sha256':sha(dec),'decoded':info,'returncode':0})
report={'schema':'faac-upstream098-fixed8-smoke-v1','source_commit':'098e8900289b87d1592a859351ca0bd420397d4b','decimation':4,'input_rate':rate,'synthetic_input_frames':N,'encoder_paths':encoders,'fdkdec_path':fdk,'rows':rows}
p=root/'smoke-results.json';p.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
