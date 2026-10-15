#!/usr/bin/env python3
"""Strictly validate all 392 mode-1 full-corpus AAC streams after scoring."""
import hashlib,json,subprocess,tempfile,os
from concurrent.futures import ThreadPoolExecutor,as_completed
from pathlib import Path
OUT=Path(__file__).resolve().parent; ROWS=OUT/'rows.json'; RESULT=OUT/'strict-interop.json'
def sha(p):
 h=hashlib.sha256()
 with Path(p).open('rb') as f:
  for b in iter(lambda:f.read(1<<20),b''):h.update(b)
 return h.hexdigest()
def main():
 cfg=json.loads((OUT/'config.json').read_text());manifest=json.loads(Path('/private/tmp/faac-upstream098-adaptive-border/probe-results/manifest.json').read_text())
 rows=json.loads(ROWS.read_text())['rows']; faad=Path(manifest['binaries']['faad']['path']);ffmpeg=Path(manifest['binaries']['ffmpeg']['path'])
 if len(rows)!=392:raise RuntimeError(f'expected 392 scored streams, got {len(rows)}')
 if sha(faad)!=manifest['binaries']['faad']['sha256'] or sha(ffmpeg)!=manifest['binaries']['ffmpeg']['sha256']:raise RuntimeError('strict decoder changed from frozen manifest')
 results={};errors=[]
 def one(pair):
  key,row=pair;p=Path(row['encoded_path'])
  if not p.is_file() or sha(p)!=row['encoded_sha256']:raise RuntimeError(f'encoded artifact hash changed: {key}')
  with tempfile.TemporaryDirectory(prefix='faac-mode1-strict-') as td:
   dec=Path(td)/'faad.wav'; f=subprocess.run([str(faad),'--strict','--overwrite','-o',str(dec),str(p)],capture_output=True,text=True)
   faad_ok=f.returncode==0 and dec.is_file()
   x=subprocess.run([str(ffmpeg),'-v','error','-xerror','-i',str(p),'-f','null','-'],capture_output=True,text=True)
   ff_ok=x.returncode==0 and not x.stderr.strip()
  return key,{'faad_strict_pass':faad_ok,'faad_returncode':f.returncode,'ffmpeg_strict_pass':ff_ok,'ffmpeg_returncode':x.returncode,'encoded_sha256':row['encoded_sha256'],'profile':row['profile'],'actual_payload_kbps':row['actual_payload_kbps']}
 with ThreadPoolExecutor(max_workers=2) as pool:
  futures={pool.submit(one,pair):pair[0] for pair in rows.items()}
  for future in as_completed(futures):
   key=futures[future]
   try:k,v=future.result();results[k]=v
   except Exception as e:errors.append({'key':key,'error':repr(e)})
   if len(results)+len(errors)==392 or len(results)%16==0:
    tmp=RESULT.with_suffix('.json.tmp');tmp.write_text(json.dumps({'schema':1,'source_rows_sha256':sha(ROWS),'faad_sha256':sha(faad),'ffmpeg_sha256':sha(ffmpeg),'rows':results,'errors':errors},indent=2,sort_keys=True)+'\n');os.replace(tmp,RESULT)
    print(f'checked {len(results)+len(errors)}/392; errors={len(errors)}',flush=True)
 if errors or len(results)!=392:raise RuntimeError(f'incomplete strict pass {len(results)}/392 errors={len(errors)}')
 summary={'rows':392,'faad_pass':sum(r['faad_strict_pass'] for r in results.values()),'ffmpeg_pass':sum(r['ffmpeg_strict_pass'] for r in results.values()),'errors':0}
 tmp=RESULT.with_suffix('.json.tmp');tmp.write_text(json.dumps({'schema':1,'source_rows_sha256':sha(ROWS),'faad_sha256':sha(faad),'ffmpeg_sha256':sha(ffmpeg),'summary':summary,'rows':results,'errors':errors},indent=2,sort_keys=True)+'\n');os.replace(tmp,RESULT)
 print(json.dumps(summary,indent=2))
if __name__=='__main__':main()
