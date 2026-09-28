#!/usr/bin/env python3
"""Four-clip off-identity and injected-decode gate, serial by clip/rate."""
import csv, hashlib, os, re, subprocess, sys
from pathlib import Path
import numpy as np, soundfile as sf
sys.path.insert(0, '/private/tmp/claude-501/faac-work/transplant/probe')
from grid_score import grid_match
ROOT=Path(__file__).resolve().parent/'gate_four';ROOT.mkdir(exist_ok=True)
CORPUS=Path('/Users/nschimme/gitprojects/faac-benchmark/data/external/audio')
CLIPS=['Robots_old.16b48k.wav','Girl_In_The_Fire__Sample_.16b48k.wav','27-last-song-drums-and-trampets.441.16b48k.wav','21-classic.441.16b48k.wav','Greatest_Love_of_All_2min57.16b48k.wav']
FAAD='/private/tmp/claude-501/faac-work/faad3/bs/frontend/faad'
ARMS={'A':('/private/tmp/price-grid-pristine-A/build/frontend/faac','/private/tmp/price-grid-pristine-master/build/frontend/faac'),
      'B':('/private/tmp/claude-501/faac-work/price-grid/build/frontend/faac','/private/tmp/price-grid-pristine-579/build/frontend/faac')}
def run(cmd,env=None):
 e={k:v for k,v in os.environ.items() if not k.startswith('FAAC_SBR_INJECT') and k!='FAAC_PRICE_DUMP' and k!='FAAD_DUMP'};e.update(env or {})
 p=subprocess.run([str(c) for c in cmd],capture_output=True,text=True,env=e)
 if p.returncode:raise RuntimeError(f'HARNESS ERROR: command {cmd}: {p.stderr[-1800:]}')
 return p
rows=[]
for clip in CLIPS:
 src=CORPUS/clip;x,sr=sf.read(src,dtype='float32',always_2d=True)
 if sr!=48000 or x.shape[1]!=2:raise RuntimeError(f'HARNESS ERROR: source {src}')
 cd=ROOT/clip;cd.mkdir(exist_ok=True);pad=cd/'pad.wav';sf.write(pad,np.vstack((np.zeros((33,2),np.float32),x)),48000,subtype='FLOAT')
 for rate in (40,48,64):
  d=cd/str(rate);d.mkdir(exist_ok=True);fdk=d/'fdk.aac';don=d/'fdk.dump';don.unlink(missing_ok=True)
  run(['fdkaac','-p','5','-b',rate*1000,'-f','2','-o',fdk,pad])
  run([FAAD,'--strict','-q','-b','32f','-o',d/'fdk.wav',fdk],{'FAAD_DUMP':str(don)})
  for arm,(exe,pristine) in ARMS.items():
   files=[]
   for name,binary in ((arm,exe),('P',pristine)):
    out=d/f'{name}-off.m4a';run([binary,'--overwrite','--object-type','he-aac-v1','-b',rate,'-o',out,src]);files.append(out)
   md=[hashlib.md5(p.read_bytes()).hexdigest() for p in files]
   if md[0]!=md[1]:raise RuntimeError(f'HARNESS ERROR: {arm} off identity {clip} {rate}: {md}')
   aac=d/f'{arm}-inject.aac';dump=d/f'{arm}.dump';price=d/f'{arm}.price';price.unlink(missing_ok=True);dump.unlink(missing_ok=True)
   run([exe,'--overwrite','--object-type','he-aac-v1','-b',rate,'-o',aac,src],{'FAAC_SBR_INJECT':str(don),'FAAC_SBR_INJECT_FIELDS':'grid','FAAC_SBR_INJECT_OFFSET':'1','FAAC_PRICE_DUMP':str(price)})
   info=run(['ffprobe','-v','error','-select_streams','a:0','-show_entries','stream=profile,sample_rate,channels','-of','default=noprint_wrappers=1',aac]).stdout
   if not all(q in info for q in ('profile=HE-AAC','sample_rate=48000','channels=2')):raise RuntimeError(f'HARNESS ERROR: profile {aac}: {info}')
   diag=run([FAAD,'--strict','-q','-b','32f','-o',d/f'{arm}.wav',aac],{'FAAD_DUMP':str(dump)}).stderr
   if not re.search(r'Error concealment\s+: 0 frames',diag) or not re.search(r'non-END termination: 0',diag):raise RuntimeError(f'HARNESS ERROR: FAAD decode {aac}: {diag[-1200:]}')
   run(['ffmpeg','-v','error','-xerror','-err_detect','explode','-i',aac,'-f','null','-'])
   grid=grid_match(don,dump)
   if min(grid)<98:raise RuntimeError(f'HARNESS ERROR: grid {aac}: {grid}')
   closes=sum(l.startswith('X ') for l in price.read_text().splitlines())
   rows.append((clip,rate,arm,*md,*grid,closes))
   print(clip,rate,arm,'PASS',grid,'closes',closes,flush=True)
with (ROOT/'results.csv').open('w',newline='') as f:
 w=csv.writer(f);w.writerow(('clip','rate','arm','md5','pristine_md5','grid0','grid1','closing_grids'));w.writerows(rows)
