#!/usr/bin/env python3
"""Score the eight fixed transient-border split controls on the four-clip gate."""
from __future__ import annotations
import argparse, hashlib, json, math, os, shutil, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path('/Users/nschimme/gitprojects/faac')
ARCHIVE = ROOT / '.cache/he-v1-feature-study/transient-border/fixed8-full-v1'
BENCH = Path('/Users/nschimme/gitprojects/faac-benchmark')
CLIPS = tuple(sorted(path.name for path in (BENCH / 'data/external/audio').glob('*.wav')))
RATES = (32,48,64,96)
COUNTS = (8,)
VARIANTS = {}
for ctx,root in [('base','/private/tmp/faac-border-fixed{n}-base'),('stack','/private/tmp/faac-border-fixed{n}-stack')]:
    for n in COUNTS:
        d=Path(root.format(n=n))
        VARIANTS[f'fixed{n}_{ctx}']={'binary':d/'build-border/frontend/faac','library':d/'build-border/libfaac/libfaac.2.dylib','variant_json':d/'probe/variant.json'}
BASE_RESULTS=Path('/private/tmp/faac-he-v1-probes/probe/results-all/feature-results.json')
STACK_RESULTS=ROOT/'.cache/he-v1-feature-study/stack/results-all/stack-results.json'
BASE_KEY='baseline_default'; STACK_KEY='stack18'
BASE_BIN=Path('/private/tmp/faac-he-v1-probes/build-baseline/frontend/faac'); BASE_LIB=Path('/private/tmp/faac-he-v1-probes/build-baseline/libfaac/libfaac.2.dylib')
STACK_BIN=Path('/private/tmp/faac-he-v1-candidate/build-candidate/frontend/faac'); STACK_LIB=Path('/private/tmp/faac-he-v1-candidate/build-candidate/libfaac/libfaac.2.dylib')
FDK_BIN=BENCH/'bin/fdkdec'; FAAD_BIN=Path('/private/tmp/faac-he-v1-decoder-build/frontend/faad'); FFMPEG=shutil.which('ffmpeg')
ALLOW=('NUMBA_DISABLE_JIT','CCACHE_DISABLE')

def sha(p):
 h=hashlib.sha256(); f=Path(p).open('rb')
 for b in iter(lambda:f.read(1<<20),b''):h.update(b)
 return h.hexdigest()
def atomic(path,obj):
 path.parent.mkdir(parents=True,exist_ok=True); tmp=path.with_suffix(path.suffix+'.tmp'); tmp.write_text(json.dumps(obj,indent=2,sort_keys=True)+'\n'); os.replace(tmp,path)
def matrix(): return {k:{} for k in VARIANTS}
def identity(runner,ec):
 return {'runner_sha256':sha(runner),'benchmark_commit':ec.git_head(BENCH),'clips':list(CLIPS),'clip_sha256':{c:sha(BENCH/'data/external/audio'/c) for c in CLIPS},'rates_kbps':list(RATES),'variants':{k:{'binary_sha256':sha(v['binary']),'library_sha256':sha(v['library']),'variant_manifest_sha256':sha(v['variant_json']),'variant_manifest':json.loads(v['variant_json'].read_text())} for k,v in VARIANTS.items()},'baseline_binary_sha256':sha(BASE_BIN),'baseline_library_sha256':sha(BASE_LIB),'stack_binary_sha256':sha(STACK_BIN),'stack_library_sha256':sha(STACK_LIB),'fdk_sha256':sha(FDK_BIN),'faad_sha256':sha(FAAD_BIN),'ffmpeg_sha256':sha(FFMPEG),'baseline_rows_sha256':sha(BASE_RESULTS),'stack_rows_sha256':sha(STACK_RESULTS),'matrix':{'feature_conditions':list(VARIANTS),'reference_conditions':['baseline_ref','stack_ref'],'expected_rows':(len(VARIANTS)+2)*len(CLIPS)*len(RATES)},'scoring':'strict FDK before Zimtohrli; actual AAC packet payload bitrate; PCHIP BD-rate and 49-clip bootstrap; strict FAAD3 and FFmpeg after scoring; no inherited FAAC_SBR_* environment flags'}
def envclean():
 e={'PATH':os.environ.get('PATH','/usr/bin:/bin'),'LANG':'C','LC_ALL':'C'}
 for k in ALLOW:
  if k in os.environ:e[k]=os.environ[k]
 return e

def main():
 parser=argparse.ArgumentParser(); parser.add_argument('--start',action='store_true'); parser.add_argument('--jobs',type=int,default=2); parser.add_argument('--output-dir',type=Path,default=ARCHIVE); args=parser.parse_args()
 sys.path.insert(0,str(ROOT/'.cache/he-v1-feature-study/crossframe-border/tools'))
 import run_crossframe_gate as common
 ec=common.envcount_module(); out=args.output_dir.resolve(); out.mkdir(parents=True,exist_ok=True)
 ident=identity(Path(__file__).resolve(),ec); cp=out/'config.json'; old=json.loads(cp.read_text()) if cp.exists() else None
 if old and old.get('immutable_run_config')!=ident: raise RuntimeError('immutable fixed-split config changed')
 atomic(cp,{'schema':1,'created_utc':datetime.now(timezone.utc).isoformat(),'immutable_run_config':ident,'scoring_enabled':args.start,'workers':args.jobs,'output_dir':str(out)})
 if not args.start: print(f'Prepared only: {cp}'); return 0
 sys.path.insert(0,str(BENCH)); sys.path.insert(0,str(BENCH/'scripts'))
 from codec_bench.encoders import FAACEncoder
 from codec_bench.decoders import FDKDecoder
 import utils
 from compare_results import bootstrap_mean_ci
 from phase2_mos import score_wav_pair
 from bd_rate import bd_rate_curve
 base_doc=json.loads(BASE_RESULTS.read_text()); stack_doc=json.loads(STACK_RESULTS.read_text())
 rows={}
 for clip in CLIPS:
  for rate in RATES:
   for label,doc,key in [('baseline_ref',base_doc,BASE_KEY),('stack_ref',stack_doc,STACK_KEY)]:
    oldrow=doc['rows'][f'{key}|{clip}|{rate}']; ep=Path(oldrow['encoded_path']); dp=Path(oldrow['decoded_path'])
    if sha(ep)!=oldrow['encoded_sha256'] or sha(dp)!=oldrow['decoded_sha256']: raise RuntimeError(f'reference changed {key}/{clip}/{rate}')
    if ec.profile(ep)!='HE-AAC' or ec.payload_identity(ep)!=oldrow['payload_sha256']: raise RuntimeError(f'reference identity/profile failure {key}/{clip}/{rate}')
    rows[f'{label}|{clip}|{rate}']={'variant':label,'clip':clip,'target_kbps':rate,'actual_payload_kbps':oldrow['actual_payload_kbps'],'profile':'HE-AAC','mos':float(oldrow['mos']),'metric_backend':oldrow.get('metric_backend','zimtohrli'),'mos_source':f'archived-reference:{key}','payload_sha256':oldrow['payload_sha256'],'decoded_sha256':oldrow['decoded_sha256'],'encoded_path':str(ep),'encoded_sha256':oldrow['encoded_sha256'],'decoded_path':str(dp),'decoded_file_sha256':oldrow['decoded_sha256'],'fdk_status':'previously strict-decoded','environment':{},'command':oldrow.get('command',[])}
 refdir=out/'reference-wav'; refdir.mkdir(exist_ok=True); refs={}
 for clip in CLIPS:
  p=refdir/clip; src=BENCH/'data/external/audio'/clip
  if not p.exists() and not utils.wav_conv(str(src),str(p),rate=48000,channels=2): raise RuntimeError(f'reference PCM conversion failed: {clip}')
  refs[clip]=p
 rowpath=out/'rows.json'; atomic(rowpath,{'schema':1,'rows':rows})
 def work(label,clip,rate):
  v=VARIANTS[label]; audio=BENCH/'data/external/audio'/clip
  enc=out/'encodes'/label/str(rate)/(Path(clip).stem+'.m4a'); dec=out/'fdk-wav'/label/str(rate)/(Path(clip).stem+'.wav'); enc.parent.mkdir(parents=True,exist_ok=True); dec.parent.mkdir(parents=True,exist_ok=True)
  encoder=FAACEncoder('faac-fixed-border-gate',str(v['binary']),profile='he'); cmd=encoder.get_encode_cmd(str(audio),str(enc),rate,2,48000)
  if '--object-type' not in cmd or cmd[cmd.index('--object-type')+1]!='he-aac-v1': raise RuntimeError(f'not forced HE-v1: {cmd}')
  # Explicit empty feature environment: strip every FAAC_SBR_* inherited value.
  env=envclean(); [env.pop(k,None) for k in list(env) if k.startswith('FAAC_SBR_')]
  done=subprocess.run(cmd,check=True,capture_output=True,text=True,env=env)
  if ec.profile(enc)!='HE-AAC': raise RuntimeError(f'profile mismatch: {label}/{clip}/{rate}')
  with tempfile.TemporaryDirectory(prefix='fixed-border-fdk-') as td: ec.decode_fdk(enc,dec,FDKDecoder('FDK AAC',str(FDK_BIN)),utils,Path(td))
  packet=ec.payload_identity(enc); pcm=sha(dec); payload=ec.payload_bitrate(enc,ec.wav_duration(audio))
  return {'variant':label,'clip':clip,'target_kbps':rate,'actual_payload_kbps':payload,'profile':'HE-AAC','payload_sha256':packet,'decoded_sha256':pcm,'encoded_path':str(enc),'encoded_sha256':sha(enc),'decoded_path':str(dec),'decoded_file_sha256':pcm,'fdk_status':'strict decoded to 48 kHz stereo','command':cmd,'stderr_tail':done.stderr[-1000:],'environment':{},'mos':None,'metric_backend':None,'mos_source':'pending'}
 jobs=[(label,c,r) for label in VARIANTS for c in CLIPS for r in RATES]
 with ThreadPoolExecutor(max_workers=args.jobs) as pool:
  futures={pool.submit(work,*j):j for j in jobs}
  for f in as_completed(futures):
   label,clip,rate=futures[f]; row=f.result(); ref='stack_ref' if label.endswith('_stack') else 'baseline_ref'; reference=rows[f'{ref}|{clip}|{rate}']
   # Reuse MOS only on exact packet AND decoded PCM identity.
   if row['payload_sha256']==reference['payload_sha256'] and row['decoded_sha256']==reference['decoded_sha256']:
    row['mos']=reference['mos']; row['metric_backend']=reference['metric_backend']; row['mos_source']=f'identity:{ref}|{clip}|{rate}'
   else:
    mos,backend=score_wav_pair(str(refs[clip]),row['decoded_path'],mode_str='audio',sample_rate=48000)
    if mos is None or backend!='zimtohrli' or not math.isfinite(float(mos)): raise RuntimeError(f'MOS failed: {label}/{clip}/{rate}')
    row['mos']=float(mos); row['metric_backend']=backend; row['mos_source']='scored'
   rows[f'{label}|{clip}|{rate}']=row
   atomic(rowpath,{'schema':1,'rows':rows})
   print(f'completed {label}|{clip}|{rate} payload={row["actual_payload_kbps"]:.3f} MOS={row["mos"]:.6f} {row["mos_source"]}',flush=True)
 common.feature_matrix=matrix
 interop=common.validate_interop(ec,rows,out,FFMPEG)
 summary=common.summarize(ec,rows,out,bootstrap_mean_ci,bd_rate_curve); summary['interop']={k:interop[k] for k in ('streams','faad_pass','ffmpeg_pass')}; atomic(out/'summary.json',summary)
 print(json.dumps(summary,indent=2,sort_keys=True)); return 0
if __name__=='__main__': raise SystemExit(main())
