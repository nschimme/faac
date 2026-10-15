#!/usr/bin/env python3
"""Full 49x4 mode-1 adaptive experiment; requires explicit --execute."""
from __future__ import annotations
import argparse,hashlib,json,math,os,shutil,statistics,subprocess,sys,tempfile,wave
from concurrent.futures import ThreadPoolExecutor,as_completed
from pathlib import Path
ROOT=Path('/Users/nschimme/gitprojects/faac'); BENCH=Path('/Users/nschimme/gitprojects/faac-benchmark')
OUT=Path(__file__).resolve().parent; CONFIG=OUT/'config.json'
FULL=ROOT/'.cache/he-v1-feature-study/transient-border/upstream098-fixed8-gate-v2/full49/rows.json'
RATES=(32,48,64,96); LAMBDAS=('0.001','0.003')
def sha(path):
 h=hashlib.sha256()
 with Path(path).open('rb') as f:
  for b in iter(lambda:f.read(1<<20),b''):h.update(b)
 return h.hexdigest()
def run(cmd,env):return subprocess.run(cmd,check=True,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,env=env)
def packet_payload(path):
 d=json.loads(run(['ffprobe','-v','error','-select_streams','a:0','-show_packets','-show_data_hash','sha256','-show_entries','packet=size,data_hash','-of','json',str(path)],CLEAN_ENV).stdout)
 packets=d.get('packets',[])
 if not packets:raise RuntimeError(f'no AAC packets: {path}')
 h=hashlib.sha256(); total=0
 for p in packets:
  size=int(p['size']);total+=size;h.update(f"{size}:{p['data_hash']}\n".encode())
 return h.hexdigest(),total
def profile(path):
 d=json.loads(run(['ffprobe','-v','error','-select_streams','a:0','-show_entries','stream=profile','-of','json',str(path)],CLEAN_ENV).stdout)
 return d['streams'][0].get('profile','') if len(d.get('streams',[]))==1 else ''
def atomic(path,obj):
 tmp=Path(str(path)+'.tmp');tmp.write_text(json.dumps(obj,indent=2,sort_keys=True)+'\n');os.replace(tmp,path)
def clean_env(flags=None):
 env={'PATH':os.environ.get('PATH','/usr/bin:/bin'),'LANG':'C','LC_ALL':'C'}
 for key in ('NUMBA_DISABLE_JIT','CCACHE_DISABLE'):
  if key in os.environ:env[key]=os.environ[key]
 env.update(flags or {});return env
CLEAN_ENV=clean_env()
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--execute',action='store_true',help='run the frozen full-corpus adaptive experiment');ap.add_argument('--workers',type=int,default=2);args=ap.parse_args()
 cfg=json.loads(CONFIG.read_text()); cfgsha=sha(CONFIG)
 if not args.execute:
  print(json.dumps({'status':'prepared; no encodes or scoring run','config_sha256':cfgsha,'expected_rows':cfg['expected_adaptive_rows'],'output_root':cfg['output_root']},indent=2));return 0
 if args.workers<1:raise RuntimeError('workers must be positive')
 rows_path=OUT/'rows.json'
 if rows_path.exists() or (OUT/'artifacts').exists():raise RuntimeError('output already exists; preserving prior artifacts, use a new versioned output directory')
 manifest=json.loads(Path('/private/tmp/faac-upstream098-adaptive-border/probe-results/manifest.json').read_text())
 full=json.loads(FULL.read_text()); refs=full['rows']; clips=tuple(cfg['clips'])
 if sha(FULL)!=cfg['full_reference_rows_sha256']:raise RuntimeError('frozen 098 baseline/fixed8 rows changed')
 if len(clips)!=49 or len(refs)!=392:raise RuntimeError('full fixed8 reference matrix malformed')
 for key in ('probe_faac','probe_libfaac'):
  item=manifest['binaries'][key]
  if sha(item['path'])!=item['sha256'] or cfg['probe_binary']['sha256']!=item['sha256'] and key=='probe_faac':raise RuntimeError(f'frozen probe binary changed: {key}')
 if sha('/private/tmp/faac-upstream098-fdk-new/fdkdec')!=cfg['decoder']['fdk_sha256']:raise RuntimeError('pinned new FDK decoder changed')
 if sha('/private/tmp/faac-upstream098-adaptive-border/probe-results/manifest.json')!=sha(Path(cfg['probe_gate_path']).parent/'manifest.json'):
  # The source manifest was frozen separately from gate.json; content equality is checked below.
  raise RuntimeError('adaptive manifest path mismatch')
 if sha(cfg['probe_gate_path'])!=cfg['probe_gate_sha256']:raise RuntimeError('frozen four-clip gate changed')
 for clip in clips:
  for rate in RATES:
   b=refs.get(f'upstream_baseline|{clip}|{rate}');f=refs.get(f'upstream_fixed8|{clip}|{rate}')
   if not b or not f or b['input_sha256']!=cfg['clip_sha256'][clip] or f['input_sha256']!=cfg['clip_sha256'][clip]:raise RuntimeError(f'missing or changed comparison reference {clip}/{rate}')
 sys.path.insert(0,str(BENCH))
 from codec_bench.encoders import FAACEncoder
 from phase2_mos import score_wav_pair
 import utils
 from scripts.bd_rate import bd_rate_curve
 from compare_results import bootstrap_mean_ci
 probe=Path(cfg['probe_binary']['path']);fdk=Path(cfg['decoder']['fdk_path'])
 ffmpeg=Path(manifest['binaries']['ffmpeg']['path']); faad=Path(manifest['binaries']['faad']['path'])
 for path,key in ((ffmpeg,'ffmpeg'),(faad,'faad')):
  if sha(path)!=manifest['binaries'][key]['sha256']:raise RuntimeError(f'{key} decoder hash changed')
 outart=OUT/'artifacts';outart.mkdir(parents=True)
 # Freeze the exact normalized original-source WAVs used by every quality score.
 anchors={}; refdir=OUT/'original-reference-48k-stereo';refdir.mkdir()
 for clip in clips:
  source=Path(refs[f'upstream_baseline|{clip}|32']['input_path'])
  if sha(source)!=cfg['clip_sha256'][clip]:raise RuntimeError(f'input WAV hash changed: {clip}')
  dst=refdir/(clip+'.wav')
  if not utils.wav_conv(str(source),str(dst),rate=48000,channels=2):raise RuntimeError(f'cannot normalize MOS source: {clip}')
  anchors[clip]={'input_path':str(source),'input_sha256':sha(source),'reference_wav_path':str(dst),'reference_wav_sha256':sha(dst),'sample_rate':48000,'channels':2}
 encoder=FAACEncoder('FAAC upstream 098 adaptive',str(probe),profile='he')
 fdksha=sha(fdk); results={}; errors=[]
 tasks=[(lam,clip,rate) for lam in LAMBDAS for clip in clips for rate in RATES]
 def one(task):
  lam,clip,rate=task; source=Path(anchors[clip]['input_path']); target=outart/f'lambda-{lam}'/str(rate)/(Path(clip).stem+'.m4a'); target.parent.mkdir(parents=True,exist_ok=True)
  decoded=target.with_suffix('.fdk-raw.wav'); pcm=target.with_suffix('.pcm48.wav')
  cmd=encoder.get_encode_cmd(str(source),str(target),rate,2,48000)
  if '--object-type' not in cmd or cmd[cmd.index('--object-type')+1]!='he-aac-v1':raise RuntimeError(f'HE v1 not forced {task}')
  env=clean_env({'FAAC_SBR_BORDER_MODE':'1','FAAC_SBR_BORDER_LAMBDA':lam})
  run(cmd,env); prof=profile(target)
  if prof!='HE-AAC':raise RuntimeError(f'{task}: ffprobe profile {prof}')
  run([str(fdk),str(target),str(decoded)],clean_env())
  if not utils.wav_conv(str(decoded),str(pcm),rate=48000,channels=2):raise RuntimeError(f'FDK PCM normalize failed {task}')
  decoded.unlink(missing_ok=True)
  packet,bytecount=packet_payload(target)
  with wave.open(str(source),'rb') as w:duration=w.getnframes()/w.getframerate()
  rate_kbps=bytecount*8.0/duration/1000.0
  mos,backend=score_wav_pair(anchors[clip]['reference_wav_path'],str(pcm),mode_str='audio',sample_rate=48000)
  if mos is None or backend!='zimtohrli' or not math.isfinite(float(mos)):raise RuntimeError(f'Zimtohrli failure {task}: {mos}/{backend}')
  base=refs[f'upstream_baseline|{clip}|{rate}'];fixed=refs[f'upstream_fixed8|{clip}|{rate}']
  row={'variant':f'mode1_lambda_{lam}','lambda':lam,'clip':clip,'target_kbps':rate,'actual_payload_kbps':rate_kbps,'profile':prof,'encoded_path':str(target),'encoded_sha256':sha(target),'packet_payload_sha256':packet,'decoded_path':str(pcm),'decoded_sha256':sha(pcm),'fdk_path':str(fdk),'fdk_sha256':fdksha,'input_path':str(source),'input_sha256':anchors[clip]['input_sha256'],'mos_anchor_path':anchors[clip]['reference_wav_path'],'mos_anchor_sha256':anchors[clip]['reference_wav_sha256'],'mos':float(mos),'mos_backend':backend,'mos_anchor':'original WAV converted to 48 kHz stereo','environment':{'FAAC_SBR_BORDER_MODE':'1','FAAC_SBR_BORDER_LAMBDA':lam},'encode_command':cmd,'reference_baseline':{'variant':'upstream_baseline','actual_payload_kbps':base['actual_payload_kbps'],'mos':base['mos'],'payload_sha256':base['packet_payload_sha256'],'decoded_sha256':base['fdk_pcm_sha256']},'reference_fixed8':{'variant':'upstream_fixed8','actual_payload_kbps':fixed['actual_payload_kbps'],'mos':fixed['mos'],'payload_sha256':fixed['packet_payload_sha256'],'decoded_sha256':fixed['fdk_pcm_sha256']},'faad_strict_pass':None,'ffmpeg_strict_pass':None}
  return f'{lam}|{clip}|{rate}',row
 with ThreadPoolExecutor(max_workers=args.workers) as pool:
  futures={pool.submit(one,t):t for t in tasks}
  for future in as_completed(futures):
   task=futures[future]
   try:key,row=future.result();results[key]=row;print(f"scored {key} MOS={row['mos']:.6f} payload={row['actual_payload_kbps']:.4f}",flush=True)
   except Exception as exc:errors.append({'task':list(task),'error':repr(exc)});print(f'ERROR {task}: {exc!r}',flush=True)
   atomic(rows_path,{'schema':1,'config_path':str(CONFIG),'config_sha256':cfgsha,'probe_manifest_sha256':sha('/private/tmp/faac-upstream098-adaptive-border/probe-results/manifest.json'),'full_reference_rows_sha256':sha(FULL),'clips':list(clips),'rates_kbps':list(RATES),'lambdas':list(LAMBDAS),'anchors':anchors,'rows':results,'errors':errors})
 if errors or len(results)!=392:raise RuntimeError(f'incomplete adaptive full run: {len(results)}/392, errors={errors}')
 summaries={}
 for lam in LAMBDAS:
  perclip=[];perrate={}
  for clip in clips:
   cand=[results[f'{lam}|{clip}|{r}'] for r in RATES]
   base=[refs[f'upstream_baseline|{clip}|{r}'] for r in RATES]
   fixed=[refs[f'upstream_fixed8|{clip}|{r}'] for r in RATES]
   bd_base=bd_rate_curve([(x['actual_payload_kbps'],x['mos']) for x in base],[(x['actual_payload_kbps'],x['mos']) for x in cand])
   bd_fixed=bd_rate_curve([(x['actual_payload_kbps'],x['mos']) for x in fixed],[(x['actual_payload_kbps'],x['mos']) for x in cand])
   if bd_base is None or bd_fixed is None:raise RuntimeError(f'invalid PCHIP BD-rate {lam}/{clip}')
   perclip.append({'clip':clip,'bd_rate_vs_baseline_percent':float(bd_base),'bd_rate_vs_fixed8_percent':float(bd_fixed)})
  for rate in RATES:
   paired=[]
   for clip in clips:
    x=results[f'{lam}|{clip}|{rate}'];b=refs[f'upstream_baseline|{clip}|{rate}'];f=refs[f'upstream_fixed8|{clip}|{rate}']
    paired.append({'clip':clip,'mos_delta_vs_baseline':x['mos']-b['mos'],'mos_delta_vs_fixed8':x['mos']-f['mos'],'payload_kbps_delta_vs_baseline':x['actual_payload_kbps']-b['actual_payload_kbps'],'payload_kbps_delta_vs_fixed8':x['actual_payload_kbps']-f['actual_payload_kbps']})
   perrate[str(rate)]={'mean_mos_delta_vs_baseline':sum(x['mos_delta_vs_baseline'] for x in paired)/49,'mean_mos_delta_vs_fixed8':sum(x['mos_delta_vs_fixed8'] for x in paired)/49,'mean_payload_delta_vs_baseline':sum(x['payload_kbps_delta_vs_baseline'] for x in paired)/49,'mean_payload_delta_vs_fixed8':sum(x['payload_kbps_delta_vs_fixed8'] for x in paired)/49,'worst_mos_vs_baseline':sorted(paired,key=lambda x:x['mos_delta_vs_baseline'])[:10],'worst_mos_vs_fixed8':sorted(paired,key=lambda x:x['mos_delta_vs_fixed8'])[:10]}
  bvals=[x['bd_rate_vs_baseline_percent'] for x in perclip];fvals=[x['bd_rate_vs_fixed8_percent'] for x in perclip]
  def metric(v):
   ci=bootstrap_mean_ci(v,n=2000,seed=0)
   return {'mean_bd_rate_percent':sum(v)/49,'median_bd_rate_percent':statistics.median(v),'clip_bootstrap_95_ci':{k:ci[k] for k in ('mean','lo','hi','n')},'negative_bd_wins':sum(x<0 for x in v),'positive_bd_losses':sum(x>0 for x in v),'zero_bd':sum(x==0 for x in v),'per_clip':v}
  summaries[lam]={'vs_baseline':metric(bvals),'vs_fixed8':metric(fvals),'per_clip':perclip,'per_rate':perrate}
 summary={'schema':1,'rows':392,'clips':49,'rates_kbps':list(RATES),'comparison':'each adaptive lambda versus fresh upstream098 baseline and fresh upstream098 fixed8; actual packet payload kbps; original-WAV Zimtohrli MOS','negative_bd_rate_is_better':True,'bootstrap':{'method':'compare_results.bootstrap_mean_ci','resamples':2000,'seed':0},'strict_interop':'pending separate 392-row FAAD3/FFmpeg pass','held_out_corpus':'none; full 49 corpus confirmation only','lambdas':summaries}
 atomic(OUT/'summary.json',summary)
 provenance={'schema':1,'config_sha256':cfgsha,'runner_sha256':sha(Path(__file__)),'rows_sha256':sha(rows_path),'summary_sha256':sha(OUT/'summary.json'),'probe_manifest_sha256':sha('/private/tmp/faac-upstream098-adaptive-border/probe-results/manifest.json'),'full_reference_rows_sha256':sha(FULL),'benchmark_commit':subprocess.check_output(['git','-C',str(BENCH),'rev-parse','HEAD'],text=True).strip(),'benchmark_helpers':{x:sha(BENCH/x) for x in ('phase2_mos.py','utils.py','scripts/bd_rate.py','compare_results.py')},'fdk_path':str(fdk),'fdk_sha256':fdksha,'faad_path':str(faad),'faad_sha256':sha(faad),'ffmpeg_path':str(ffmpeg),'ffmpeg_sha256':sha(ffmpeg),'source_anchor_hashes':anchors,'new_vs_old_fdk_gate_equivalence':{'rows':32,'status':'to-be-verified-before-run'} ,'strict_interop':'pending'}
 atomic(OUT/'provenance.json',provenance)
 print(json.dumps({'rows':392,'summary_sha256':sha(OUT/'summary.json'),'provenance_sha256':sha(OUT/'provenance.json')},indent=2))
if __name__=='__main__':main()
