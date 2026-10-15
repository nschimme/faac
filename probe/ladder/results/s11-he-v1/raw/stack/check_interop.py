import hashlib, json, subprocess, tempfile
from pathlib import Path
root=Path('/private/tmp/faac-he-v1-stack/probe')
faad=Path('/private/tmp/faac-he-v1-decoder-build/frontend/faad')
rows=[]
for kind in ('gate','all'):
    results=json.loads((root/f'results-{kind}/stack-results.json').read_text())['rows']
    for label in ('limiter','static18','static24','stack18','stack24'):
        for key,row in sorted(results.items()):
            if not key.startswith(label+'|'): continue
            enc=Path(row['encoded_path'])
            with tempfile.TemporaryDirectory(prefix='stack-interop-') as td:
                wav=Path(td)/'faad.wav'
                a=subprocess.run([str(faad),'--strict','--quiet','--overwrite','-o',str(wav),str(enc)],capture_output=True,text=True)
                b=subprocess.run(['ffmpeg','-v','error','-y','-i',str(enc),'-f','null','-'],capture_output=True,text=True)
                rows.append({'set':kind,'key':key,'encoded_sha256':hashlib.sha256(enc.read_bytes()).hexdigest(),'faad_ok':a.returncode==0 and wav.is_file() and wav.stat().st_size>0,'faad_rc':a.returncode,'faad_stderr':a.stderr[-1000:],'ffmpeg_ok':b.returncode==0,'ffmpeg_rc':b.returncode,'ffmpeg_stderr':b.stderr[-1000:]})
            print(f'{kind} {key}: FAAD={a.returncode} FFmpeg={b.returncode}',flush=True)
out={'counts':{'streams':len(rows),'faad_pass':sum(x['faad_ok'] for x in rows),'faad_fail':sum(not x['faad_ok'] for x in rows),'ffmpeg_pass':sum(x['ffmpeg_ok'] for x in rows),'ffmpeg_fail':sum(not x['ffmpeg_ok'] for x in rows)},'streams':rows}
(root/'interop-results.json').write_text(json.dumps(out,indent=2)+'\n')
if out['counts']['faad_fail'] or out['counts']['ffmpeg_fail']: raise SystemExit(1)
