import csv,hashlib,subprocess
from pathlib import Path
root=Path(__file__).parent/'xover_identity';root.mkdir(exist_ok=True)
clips=['Robots_old.16b48k.wav','Girl_In_The_Fire__Sample_.16b48k.wav','27-last-song-drums-and-trampets.441.16b48k.wav','21-classic.441.16b48k.wav','Greatest_Love_of_All_2min57.16b48k.wav']
corpus=Path('/Users/nschimme/gitprojects/faac-benchmark/data/external/audio')
exes={'base':Path('/private/tmp/claude-501/faac-work/price-grid/build/frontend/faac'),'probe':Path(__file__).parents[1]/'build/frontend/faac'}
rows=[]
for clip in clips:
 for rate in (40,48,56,64):
  hashes={}
  for name,exe in exes.items():
   path=root/f'{Path(clip).stem}.{rate}.{name}.aac'
   p=subprocess.run([exe,'--overwrite','--object-type','he-aac-v1','-b',str(rate),'-o',path,corpus/clip],capture_output=True)
   if p.returncode:raise RuntimeError('HARNESS ERROR: '+p.stderr.decode()[-1000:])
   hashes[name]=hashlib.sha256(path.read_bytes()).hexdigest()
  if hashes['base']!=hashes['probe']:raise RuntimeError(f'HARNESS ERROR: off-knob identity {clip} {rate} {hashes}')
  rows.append((clip,rate,hashes['base']))
  print('PASS',clip,rate,flush=True)
with (root/'results.csv').open('w') as f:
 w=csv.writer(f);w.writerow(('clip','rate','sha256'));w.writerows(rows)
