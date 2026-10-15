from pathlib import Path
import sys,os,subprocess,wave,json,collections
import numpy as np
sys.path.insert(0,'/Users/nschimme/gitprojects/faac-benchmark')
from codec_bench.encoders import FAACEncoder
from codec_bench.decoders import FDKDecoder
p=Path('/private/tmp/faac-he-v1-grid/probe/synthetic');p.mkdir(exist_ok=True)
rng=np.random.default_rng(0);n=48000*4
noise=rng.normal(0,.06,(n,2));clicks=rng.normal(0,.00001,(n,2))
for frame in range(n//2048):
 offset=(frame%16)*128
 clicks[frame*2048+offset:frame*2048+offset+8]=.9
cases={'clicks':clicks,'noise':noise}
enc=FAACEncoder('grid','/private/tmp/faac-he-v1-grid/build-probe/frontend/faac',profile='he')
dec=FDKDecoder('fdk','/Users/nschimme/gitprojects/faac-benchmark/bin/fdkdec')
rows=[]
for name,data in cases.items():
 src=p/(name+'.wav')
 with wave.open(str(src),'wb') as w:w.setnchannels(2);w.setsampwidth(2);w.setframerate(48000);w.writeframes((np.clip(data,-1,1)*32767).astype('<i2').tobytes())
 for variant,flags in [('grids',{'FAAC_SBR_GRID_LATE_FIXVAR':'1','FAAC_SBR_GRID_CENTER_VARVAR':'1'}),('flat',{'FAAC_SBR_GRID_FLATLOW_20':'1'})]:
  out=p/(name+'-'+variant+'.m4a');env={k:v for k,v in os.environ.items() if not k.startswith('FAAC_SBR_')};env.update(flags,FAAC_SBR_GRID_TRACE='1')
  r=subprocess.run(enc.get_encode_cmd(str(src),str(out),64,2,48000),env=env,capture_output=True,text=True,check=True)
  (p/(name+'-'+variant+'.trace')).write_text(r.stderr)
  hist=collections.Counter(line for line in r.stderr.splitlines() if line.startswith('SBR grid '))
  fdk=p/'fdk.wav';e=dict(os.environ);e.update(dec.get_run_env());subprocess.run(dec.get_decode_cmd(str(out),str(fdk)),env=e,capture_output=True,check=True)
  faad=p/'faad.wav';subprocess.run(['/private/tmp/faac-he-v1-decoder-build/frontend/faad','--strict','--overwrite','-o',str(faad),str(out)],capture_output=True,check=True)
  subprocess.run(['ffmpeg','-v','error','-xerror','-i',str(out),'-f','null','-'],capture_output=True,check=True)
  rows.append({'case':name,'variant':variant,'histogram':hist,'fdk_pass':True,'faad_pass':True,'ffmpeg_pass':True})
(p/'results.json').write_text(json.dumps(rows,indent=2));print(json.dumps(rows,indent=2))
