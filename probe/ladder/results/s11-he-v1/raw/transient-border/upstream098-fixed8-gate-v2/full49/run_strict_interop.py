#!/usr/bin/env python3
"""Strict FAAD3 and FFmpeg checks over the 392 freshly scored full49 streams."""
import hashlib,json,subprocess,tempfile,os,shutil
from concurrent.futures import ThreadPoolExecutor,as_completed
from pathlib import Path
OUT=Path(__file__).resolve().parent; ROWS=OUT/'rows.json'; RESULT=OUT/'strict-interop.json'
FAAD=Path('/private/tmp/faac-he-v1-decoder-build/frontend/faad'); FFMPEG=Path(shutil.which('ffmpeg'))
def sha(p):
 h=hashlib.sha256()
 with Path(p).open('rb') as f:
  for b in iter(lambda:f.read(1<<20),b''):h.update(b)
 return h.hexdigest()
def main():
 d=json.loads(ROWS.read_text()); rows=d['rows']
 if len(rows)!=392 or d['errors']:raise RuntimeError('full 392 source rows are incomplete')
 results={}; errors=[]
 def one(item):
  key,row=item; p=Path(row['encoded_path'])
  if not p.is_file() or sha(p)!=row['encoded_sha256']:raise RuntimeError(f'encoded artifact changed: {key}')
  with tempfile.TemporaryDirectory(prefix='faac-strict-') as td:
   out=Path(td)/'faad.wav'
   f=subprocess.run([str(FAAD),'--strict','--overwrite','-o',str(out),str(p)],capture_output=True,text=True)
   faad=f.returncode==0 and out.is_file()
   ff=subprocess.run([str(FFMPEG),'-v','error','-xerror','-i',str(p),'-f','null','-'],capture_output=True,text=True)
   ffmpeg=ff.returncode==0 and not ff.stderr.strip()
  return key,{'faad_strict_pass':faad,'faad_rc':f.returncode,'ffmpeg_strict_pass':ffmpeg,'ffmpeg_rc':ff.returncode,'encoded_sha256':row['encoded_sha256']}
 with ThreadPoolExecutor(max_workers=2) as pool:
  fs={pool.submit(one,it):it[0] for it in rows.items()}
  for future in as_completed(fs):
   key=fs[future]
   try: k,v=future.result();results[k]=v
   except Exception as e:errors.append({'key':key,'error':repr(e)})
   if len(results)%16==0 or len(results)+len(errors)==392:
    tmp=RESULT.with_suffix('.json.tmp');tmp.write_text(json.dumps({'schema':1,'rows_source_sha256':sha(ROWS),'faad_binary_sha256':sha(FAAD),'ffmpeg_binary_sha256':sha(FFMPEG),'rows':results,'errors':errors},indent=2,sort_keys=True)+'\n');os.replace(tmp,RESULT)
    print(f"checked {len(results)+len(errors)}/392 errors={len(errors)}",flush=True)
 if errors or len(results)!=392:raise RuntimeError(f'incomplete strict run {len(results)}/392, errors={len(errors)}')
 summary={'rows':392,'faad_pass':sum(x['faad_strict_pass'] for x in results.values()),'ffmpeg_pass':sum(x['ffmpeg_strict_pass'] for x in results.values()),'errors':0}
 tmp=RESULT.with_suffix('.json.tmp');tmp.write_text(json.dumps({'schema':1,'source_rows_sha256':sha(ROWS),'faad_binary_sha256':sha(FAAD),'ffmpeg_binary_sha256':sha(FFMPEG),'summary':summary,'rows':results,'errors':errors},indent=2,sort_keys=True)+'\n');os.replace(tmp,RESULT)
 print(json.dumps(summary,indent=2))
if __name__=='__main__':main()
