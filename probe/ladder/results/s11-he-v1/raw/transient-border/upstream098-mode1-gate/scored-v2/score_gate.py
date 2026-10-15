#!/usr/bin/env python3
"""Score adaptive mode-1 gate WAVs against original inputs and compare to fresh 098 controls."""
import hashlib,json,math,os,subprocess,sys,tempfile,statistics
from concurrent.futures import ThreadPoolExecutor,as_completed
from pathlib import Path
ROOT=Path('/Users/nschimme/gitprojects/faac');BENCH=Path('/Users/nschimme/gitprojects/faac-benchmark')
GATE=Path('/private/tmp/faac-upstream098-adaptive-border/probe-results/gate.json')
FULL=ROOT/'.cache/he-v1-feature-study/transient-border/upstream098-fixed8-gate-v2/full49'
OUT=Path(__file__).resolve().parent; RATES=(32,48,64,96); LAMBDAS=('0.001','0.003')
def sha(p):
 h=hashlib.sha256()
 with Path(p).open('rb') as f:
  for b in iter(lambda:f.read(1<<20),b''):h.update(b)
 return h.hexdigest()
def main():
 sys.path.insert(0,str(BENCH))
 import utils
 from phase2_mos import score_wav_pair
 from scripts.bd_rate import bd_rate_curve
 from compare_results import bootstrap_mean_ci
 gate=json.loads(GATE.read_text())['rows']; full=json.loads((FULL/'rows.json').read_text())['rows']
 if len(gate)!=16:raise RuntimeError(f'expected 16 clip/rate control rows, got {len(gate)}')
 clips=tuple(sorted({k.split('|')[0] for k in gate})); candidates=[]
 for clip in clips:
  for rate in RATES:
   d=gate[f'{clip}|{rate}']
   if d['unset']['profile']!='HE-AAC' or not d['unset']['baseline_packet_match'] or not d['unset']['baseline_pcm_match']:
    raise RuntimeError(f'unset control failed exact baseline identity: {clip}/{rate}')
   for lam in LAMBDAS:
    r=d['adaptive'][lam]; src=Path(r['source_wav']);dec=Path(r['fdk_path'])
    if sha(src)!=r['source_sha256'] or sha(dec)!=r['decoded_sha256'] or sha(Path(r['encoded_path']))!=r['encoded_sha256']:
     raise RuntimeError(f'adaptive artifact hash changed: {clip}/{rate}/{lam}')
    if r['profile']!='HE-AAC' or not r['faad_strict_pass'] or not r['ffmpeg_strict_pass']:
     raise RuntimeError(f'adaptive strict profile/interop failure: {clip}/{rate}/{lam}')
    candidates.append((clip,rate,lam,r))
 def score(item):
  clip,rate,lam,r=item
  with tempfile.TemporaryDirectory(prefix='faac-mode1-ref-') as td:
   ref=Path(td)/'original-48k.wav'
   if not utils.wav_conv(r['source_wav'],str(ref),rate=48000,channels=2):raise RuntimeError('source conversion failed')
   refhash=sha(ref); mos,backend=score_wav_pair(str(ref),r['fdk_path'],mode_str='audio',sample_rate=48000)
  if mos is None or backend!='zimtohrli' or not math.isfinite(float(mos)):raise RuntimeError(f'MOS failed: {clip}/{rate}/{lam}: {mos}/{backend}')
  return f'{lam}|{clip}|{rate}',{'lambda':lam,'clip':clip,'target_kbps':rate,'actual_payload_kbps':r['actual_payload_kbps'],'mos':float(mos),'mos_backend':backend,'mos_anchor':'original source WAV converted to 48 kHz stereo','input_sha256':r['source_sha256'],'converted_reference_wav_sha256':refhash,'encoded_sha256':r['encoded_sha256'],'packet_payload_sha256':r['payload_sha256'],'decoded_sha256':r['decoded_sha256'],'profile':r['profile'],'faad_strict_pass':r['faad_strict_pass'],'ffmpeg_strict_pass':r['ffmpeg_strict_pass']}
 rows={}; errors=[]
 with ThreadPoolExecutor(max_workers=2) as pool:
  fs={pool.submit(score,x):x for x in candidates}
  for f in as_completed(fs):
   try:k,v=f.result();rows[k]=v;print(f"scored {k} MOS={v['mos']:.6f}",flush=True)
   except Exception as e:errors.append({'task':str(fs[f][:3]),'error':repr(e)});print('ERROR',fs[f][:3],repr(e),flush=True)
   tmp=OUT/'scores.json.tmp';tmp.write_text(json.dumps({'schema':1,'gate_path':str(GATE),'gate_sha256':sha(GATE),'source_full_rows_sha256':sha(FULL/'rows.json'),'rows':rows,'errors':errors},indent=2,sort_keys=True)+'\n');os.replace(tmp,OUT/'scores.json')
 if errors or len(rows)!=32:raise RuntimeError(f'incomplete scoring: {len(rows)}/32, errors={errors}')
 summaries={}
 for lam in LAMBDAS:
  perclip=[]; per_rate={}
  for clip in clips:
   cand=[rows[f'{lam}|{clip}|{rate}'] for rate in RATES]
   base=[full[f'upstream_baseline|{clip}|{rate}'] for rate in RATES]
   fixed=[full[f'upstream_fixed8|{clip}|{rate}'] for rate in RATES]
   bdr=bd_rate_curve([(x['actual_payload_kbps'],x['mos']) for x in base],[(x['actual_payload_kbps'],x['mos']) for x in cand])
   bdf=bd_rate_curve([(x['actual_payload_kbps'],x['mos']) for x in fixed],[(x['actual_payload_kbps'],x['mos']) for x in cand])
   if bdr is None or bdf is None:raise RuntimeError(f'invalid BD curves {lam}/{clip}')
   perclip.append({'clip':clip,'bd_rate_vs_upstream_baseline_percent':bdr,'bd_rate_vs_upstream_fixed8_percent':bdf})
  for rate in RATES:
   paired=[]
   for clip in clips:
    cand=rows[f'{lam}|{clip}|{rate}'];base=full[f'upstream_baseline|{clip}|{rate}'];fixed=full[f'upstream_fixed8|{clip}|{rate}']
    paired.append({'clip':clip,'mos_delta_vs_baseline':cand['mos']-base['mos'],'mos_delta_vs_fixed8':cand['mos']-fixed['mos'],'payload_kbps_delta_vs_baseline':cand['actual_payload_kbps']-base['actual_payload_kbps'],'payload_kbps_delta_vs_fixed8':cand['actual_payload_kbps']-fixed['actual_payload_kbps']})
   per_rate[str(rate)]={'mean_mos_delta_vs_baseline':sum(x['mos_delta_vs_baseline'] for x in paired)/len(clips),'mean_mos_delta_vs_fixed8':sum(x['mos_delta_vs_fixed8'] for x in paired)/len(clips),'worst_mos_delta_vs_baseline':sorted(paired,key=lambda x:x['mos_delta_vs_baseline'])[:4],'worst_mos_delta_vs_fixed8':sorted(paired,key=lambda x:x['mos_delta_vs_fixed8'])[:4]}
  basevals=[x['bd_rate_vs_upstream_baseline_percent'] for x in perclip];fixedvals=[x['bd_rate_vs_upstream_fixed8_percent'] for x in perclip]
  summaries[lam]={'per_clip':perclip,'vs_baseline':{'mean_bd_rate_percent':sum(basevals)/len(clips),'median_bd_rate_percent':statistics.median(basevals),'clip_bootstrap_95_ci':{k:v for k,v in bootstrap_mean_ci(basevals,n=2000,seed=0).items() if k in ('mean','lo','hi','n')},'negative_bd_wins':sum(x<0 for x in basevals),'positive_bd_losses':sum(x>0 for x in basevals)},'vs_fixed8':{'mean_bd_rate_percent':sum(fixedvals)/len(clips),'median_bd_rate_percent':statistics.median(fixedvals),'clip_bootstrap_95_ci':{k:v for k,v in bootstrap_mean_ci(fixedvals,n=2000,seed=0).items() if k in ('mean','lo','hi','n')},'negative_bd_wins':sum(x<0 for x in fixedvals),'positive_bd_losses':sum(x>0 for x in fixedvals)},'per_rate':per_rate}
 out={'schema':1,'rows':len(rows),'clips':list(clips),'rates_kbps':list(RATES),'comparison':'adaptive mode1 candidates versus fresh upstream098 baseline and fresh upstream098 fixed8, using actual AAC payload rates and original-WAV Zimtohrli MOS','strict':{'fdk_rows':len(rows),'faad_pass':sum(x['faad_strict_pass'] for x in rows.values()),'ffmpeg_pass':sum(x['ffmpeg_strict_pass'] for x in rows.values()),'unset_identity_control_rows':16},'bootstrap':{'helper':'compare_results.bootstrap_mean_ci','resamples':2000,'seed':0},'lambdas':summaries}
 (OUT/'summary.json').write_text(json.dumps(out,indent=2,sort_keys=True)+'\n');print(json.dumps(out,indent=2))
if __name__=='__main__':main()
