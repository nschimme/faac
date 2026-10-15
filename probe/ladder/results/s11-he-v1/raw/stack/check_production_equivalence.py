import concurrent.futures, hashlib, json, os, tempfile, wave
from pathlib import Path
import sys
BENCH=Path('/Users/nschimme/gitprojects/faac-benchmark')
STACK=Path('/private/tmp/faac-he-v1-stack/probe/results-all')
BASE=Path('/private/tmp/faac-he-v1-candidate/build-candidate/frontend/faac')
OUT=Path('/private/tmp/faac-he-v1-stack/probe/production-equivalence')
OUT.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(BENCH));sys.path.insert(0,'/private/tmp/faac-he-v1-stack/probe')
from codec_bench.encoders import FAACEncoder
from codec_bench.decoders import FDKDecoder
import utils
import run_stack as runner
fd=FDKDecoder('FDK AAC',str(BENCH/'bin/fdkdec'))
source=BENCH/'data/external/audio'
results=json.loads((STACK/'stack-results.json').read_text())['rows']
def sha(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def one(key,row):
    label,clip,rate=key.split('|'); src=source/clip
    dest=OUT/'encodes'/str(rate)/(Path(clip).stem+'.m4a'); dest.parent.mkdir(parents=True,exist_ok=True)
    decoded=OUT/'scoring-wav'/str(rate)/(Path(clip).stem+'.wav'); decoded.parent.mkdir(parents=True,exist_ok=True)
    env=dict(os.environ)
    for k in list(env):
        if k.startswith('FAAC_SBR_'): env.pop(k)
    encoder=FAACEncoder('faac-production',str(BASE),profile='he')
    cmd=encoder.get_encode_cmd(str(src),str(dest),rate,2,48000)
    p=__import__('subprocess').run(cmd,check=True,capture_output=True,text=True,env=env)
    profile=runner.profile(dest)
    if profile!='HE-AAC': raise RuntimeError(f'{key}: profile {profile}')
    with tempfile.TemporaryDirectory(prefix='stack-prod-fdk-') as td:
        decoded_fdk=Path(td)/(Path(clip).stem+'.wav')
        e=dict(os.environ);e.update(fd.get_run_env())
        __import__('subprocess').run(fd.get_decode_cmd(str(dest),str(decoded_fdk)),check=True,capture_output=True,text=True,env=e)
        if not decoded_fdk.is_file() or not decoded_fdk.stat().st_size: raise RuntimeError(f'{key}: empty FDK decode')
        if not utils.wav_conv(str(decoded_fdk),str(decoded),rate=48000,channels=2): raise RuntimeError(f'{key}: wav conversion failed')
    packet=runner.payload_identity(dest); pcm=sha(decoded)
    match=packet==row['payload_sha256'] and pcm==row['decoded_sha256']
    return {'key':key,'production_encoded_path':str(dest),'production_encoded_sha256':sha(dest),'production_payload_sha256':packet,'production_decoded_path':str(decoded),'production_decoded_sha256':pcm,'stack18_payload_sha256':row['payload_sha256'],'stack18_decoded_sha256':row['decoded_sha256'],'packet_match':packet==row['payload_sha256'],'pcm_match':pcm==row['decoded_sha256'],'mos_reuse_allowed':match,'mos':row['mos'] if match else None,'profile':profile,'stderr_tail':p.stderr[-2000:]}
keys=[k for k in results if k.startswith('stack18|')]
if len(keys)!=196: raise RuntimeError(f'expected 196 stack18 rows, found {len(keys)}')
rows=[]
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
    futures={pool.submit(one,k,results[k]):k for k in keys}
    for f in concurrent.futures.as_completed(futures):
        r=f.result();rows.append(r);print(f"{r['key']}: packet={r['packet_match']} pcm={r['pcm_match']}",flush=True)
rows.sort(key=lambda x:x['key'])
out={'schema':1,'production_commit':'5d1bd747','base_commit':'60bf043e','candidate_binary':str(BASE),'candidate_binary_sha256':sha(BASE),'reference_results':str(STACK/'stack-results.json'),'reference_results_sha256':sha(STACK/'stack-results.json'),'rows':rows,'counts':{'rows':len(rows),'packet_matches':sum(r['packet_match'] for r in rows),'pcm_matches':sum(r['pcm_match'] for r in rows),'both_matches':sum(r['mos_reuse_allowed'] for r in rows),'mos_reuse_allowed':sum(r['mos_reuse_allowed'] for r in rows)}}
(OUT/'production-equivalence.json').write_text(json.dumps(out,indent=2,sort_keys=True)+'\n')
print(json.dumps(out['counts'],sort_keys=True))
if out['counts']['both_matches']!=196: raise SystemExit(1)
