#!/usr/bin/env python3
"""Fresh 49-clip x 4-rung source-anchored HE-v1 comparison for upstream 098."""
import hashlib,json,os,subprocess,sys,tempfile,wave,statistics
from concurrent.futures import ThreadPoolExecutor,as_completed
from pathlib import Path
ROOT=Path('/Users/nschimme/gitprojects/faac'); BENCH=Path('/Users/nschimme/gitprojects/faac-benchmark')
OUT=Path(__file__).resolve().parent/'full49'; MANIFEST=Path('/private/tmp/faac-upstream098-build-manifest.json')
RATES=(32,48,64,96)
def sha(p):
 h=hashlib.sha256()
 with Path(p).open('rb') as f:
  for b in iter(lambda:f.read(1<<20),b''):h.update(b)
 return h.hexdigest()
def run(cmd,env=None):return subprocess.run(cmd,check=True,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,env=env)
def packet_sha(p):
 d=json.loads(run(['ffprobe','-v','error','-select_streams','a:0','-show_packets','-show_data_hash','sha256','-show_entries','packet=size,data_hash','-of','json',str(p)]).stdout)
 h=hashlib.sha256()
 for q in d['packets']:h.update(str(int(q['size'])).encode()+b':'+q['data_hash'].encode()+b'\n')
 return h.hexdigest(),sum(int(q['size']) for q in d['packets'])
def profile(p):
 d=json.loads(run(['ffprobe','-v','error','-select_streams','a:0','-show_entries','stream=profile','-of','json',str(p)]).stdout)
 return d['streams'][0]['profile'] if len(d.get('streams',[]))==1 else ''
def main():
 sys.path.insert(0,str(BENCH))
 from codec_bench.encoders import FAACEncoder
 from codec_bench.decoders import FDKDecoder
 from phase2_mos import score_wav_pair
 import utils
 from scripts.bd_rate import bd_rate_curve
 from compare_results import bootstrap_mean_ci
 manifest=json.loads(MANIFEST.read_text()); variants=manifest['variants']
 full=json.loads((ROOT/'.cache/he-v1-feature-study/transient-border/fixed8-full-v2/rows.json').read_text())['rows']
 clips=tuple(sorted({r['clip'] for r in full.values() if r['variant']=='fixed8_base'}))
 if len(clips)!=49:raise RuntimeError(f'expected 49 fixed8 corpus clips; found {len(clips)}')
 inputs={clip:{'path':str(BENCH/'data/external/audio'/clip)} for clip in clips}
 for clip in clips: inputs[clip]['sha256']=sha(inputs[clip]['path'])
 ffmpeg=subprocess.check_output(['which','ffmpeg'],text=True).strip(); faad=Path('/private/tmp/faac-he-v1-decoder-build/frontend/faad')
 fdk=FDKDecoder('FDK AAC strict',str(BENCH/'bin/fdkdec')); OUT.mkdir(parents=True,exist_ok=True); (OUT/'artifacts').mkdir(exist_ok=True)
 for n,v in variants.items():
  if sha(v['binary'])!=v['binary_sha256'] or sha(v['library'])!=v['library_sha256']:raise RuntimeError(f'{n} build changed from manifest')
 baseline_env=dict(os.environ)
 for k in list(baseline_env):
  if k.startswith('FAAC_SBR_'):baseline_env.pop(k)
 def one(task):
  variant,clip,rate=task; v=variants[variant]; src=Path(inputs[clip]['path'])
  if sha(src)!=inputs[clip]['sha256']:raise RuntimeError(f'{clip} source hash changed during run')
  outdir=OUT/'artifacts'/variant/str(rate); outdir.mkdir(parents=True,exist_ok=True)
  stem=Path(clip).stem; enc=outdir/(stem+'.m4a'); raw=outdir/(stem+'.fdk-raw.wav'); dec=outdir/(stem+'.wav')
  encoder=FAACEncoder('FAAC',v['binary'],profile='he'); cmd=encoder.get_encode_cmd(str(src),str(enc),rate,2,48000)
  run(cmd,env=baseline_env); prof=profile(enc)
  if prof!='HE-AAC':raise RuntimeError(f'{variant}|{clip}|{rate}: unexpected profile {prof}')
  run(fdk.get_decode_cmd(str(enc),str(raw)),env=baseline_env)
  if not utils.wav_conv(str(raw),str(dec),rate=48000,channels=2):raise RuntimeError(f'FDK PCM conversion failed {clip}/{rate}')
  raw.unlink(missing_ok=True); packet,bytes_n=packet_sha(enc)
  with wave.open(str(src),'rb') as wf: duration=wf.getnframes()/wf.getframerate()
  kbps=bytes_n*8/duration/1000
  ref=outdir/(stem+'.ref48k.wav')
  if not utils.wav_conv(str(src),str(ref),rate=48000,channels=2):raise RuntimeError(f'Original reference conversion failed {clip}')
  mos,backend=score_wav_pair(str(ref),str(dec),mode_str='audio',sample_rate=48000)
  ref.unlink(missing_ok=True)
  if mos is None or backend!='zimtohrli':raise RuntimeError(f'MOS failed {variant}|{clip}|{rate}: {mos}/{backend}')
  return f'{variant}|{clip}|{rate}',{'variant':variant,'clip':clip,'target_kbps':rate,'profile':prof,'actual_payload_kbps':kbps,'packet_payload_sha256':packet,'fdk_pcm_sha256':sha(dec),'encoded_path':str(enc),'encoded_sha256':sha(enc),'decoded_path':str(dec),'decoded_file_sha256':sha(dec),'input_path':str(src),'input_sha256':sha(src),'mos':float(mos),'mos_anchor':'original WAV converted to 48 kHz stereo','mos_reference_sha256':sha(src),'mos_backend':backend,'command':cmd,'fdk_strict_pass':True}
 tasks=[(variant,clip,rate) for variant in variants for clip in clips for rate in RATES]
 rows={}; errors=[]; workers=int(os.environ.get('HEV1_FULL_WORKERS','2'))
 with ThreadPoolExecutor(max_workers=workers) as pool:
  futures={pool.submit(one,t):t for t in tasks}
  for f in as_completed(futures):
   task=futures[f]
   try:
    key,row=f.result(); rows[key]=row;print(f"scored {key} MOS={row['mos']:.6f} payload={row['actual_payload_kbps']:.4f}",flush=True)
   except Exception as exc:
    errors.append({'task':task,'error':repr(exc)});print(f'ERROR {task}: {exc!r}',flush=True)
   tmp=OUT/'rows.json.tmp'; tmp.write_text(json.dumps({'schema':1,'source_manifest':str(MANIFEST),'source_manifest_sha256':sha(MANIFEST),'clips':list(clips),'rates_kbps':list(RATES),'rows':rows,'errors':errors},indent=2,sort_keys=True)+'\n');os.replace(tmp,OUT/'rows.json')
   # Incremental row hash freeze makes interrupted runs resumable and auditable.
   (OUT/'rows.sha256').write_text(sha(OUT/'rows.json')+'\n')
 if errors or len(rows)!=392:raise RuntimeError(f'incomplete full run: {len(rows)}/392 rows, {len(errors)} errors')
 bd=[]
 for clip in clips:
  base=[rows[f'upstream_baseline|{clip}|{r}'] for r in RATES]
  cand=[rows[f'upstream_fixed8|{clip}|{r}'] for r in RATES]
  if any(x['profile']!='HE-AAC' for x in base+cand):raise RuntimeError(f'profile changed {clip}')
  value=bd_rate_curve([(x['actual_payload_kbps'],x['mos']) for x in base],[(x['actual_payload_kbps'],x['mos']) for x in cand])
  if value is None:raise RuntimeError(f'PCHIP invalid on {clip}')
  bd.append({'clip':clip,'bd_rate_percent':value})
 vals=[x['bd_rate_percent'] for x in bd]
 summary={'schema':1,'rows':len(rows),'clips':len(clips),'rates_kbps':list(RATES),'mean_bd_rate_percent':sum(vals)/len(vals),'median_bd_rate_percent':statistics.median(vals),'clip_bootstrap_95_ci':bootstrap_mean_ci(vals,n=2000,seed=0),'negative_bd_wins':sum(v<-.005 for v in vals),'nonnegative_bd':sum(v>=-.005 for v in vals),'positive_bd_losses':sum(v>.005 for v in vals),'per_clip_bd_rate':bd,'strict_fdk_pass':len(rows),'scoring':'Zimtohrli audio; each row scored against source WAV converted to 48 kHz stereo; no cross-build MOS inheritance','bootstrap':{'method':'compare_results.bootstrap_mean_ci','n':2000,'seed':0}}
 (OUT/'summary.json').write_text(json.dumps(summary,indent=2,sort_keys=True)+'\n');print(json.dumps(summary,indent=2))
if __name__=='__main__':main()
