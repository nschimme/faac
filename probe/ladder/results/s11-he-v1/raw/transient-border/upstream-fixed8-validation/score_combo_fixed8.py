#!/usr/bin/env python3
"""Score the already encoded combined-PR fixed8 four-clip gate artifacts."""
from __future__ import annotations
import hashlib, json, math, os, statistics, sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
ROOT=Path('/Users/nschimme/gitprojects/faac')
BENCH=Path('/Users/nschimme/gitprojects/faac-benchmark')
ARCH=ROOT/'.cache/he-v1-feature-study/transient-border/upstream-fixed8-validation'
VALID=ARCH/'validation.json'
RATES=(32,48,64,96)

def sha(path):
 h=hashlib.sha256()
 with Path(path).open('rb') as f:
  for b in iter(lambda:f.read(1<<20),b''):h.update(b)
 return h.hexdigest()
def atomic(path,obj):
 tmp=path.with_suffix(path.suffix+'.tmp'); tmp.write_text(json.dumps(obj,indent=2,sort_keys=True)+'\n'); os.replace(tmp,path)
def main():
 sys.path[:0]=[str(BENCH),str(BENCH/'scripts')]
 from phase2_mos import score_wav_pair
 from compare_results import bootstrap_mean_ci
 from bd_rate import bd_rate_curve
 doc=json.loads(VALID.read_text()); rows=doc['rows']; keys=sorted(k for k in rows if k.startswith('combo_fixed8|'))
 if len(keys)!=16: raise RuntimeError(f'expected 16 combined rows, got {len(keys)}')
 candidates=[]
 for key in keys:
  row=rows[key]; ref=row['reference']
  expected_ref='stack_reference' if row['target_kbps']==32 else 'all_stack'
  if row['quality_reference_variant']!=expected_ref or ref['variant']!=expected_ref:
   raise RuntimeError(f'reference pairing mismatch at {key}: {row["quality_reference_variant"]} != {expected_ref}')
  if row['profile']!='HE-AAC' or ref.get('profile')!='HE-AAC' or not row.get('faad_strict_pass') or not row.get('ffmpeg_strict_pass'):
   raise RuntimeError(f'profile/interop gate failed at {key}')
  for path,expected in ((row['encoded_path'],row['encoded_sha256']),(row['decoded_path'],row['decoded_sha256']),(ref['encoded_path'],ref['encoded_sha256']),(ref['decoded_path'],ref['decoded_sha256'])):
   if not Path(path).is_file() or sha(path)!=expected: raise RuntimeError(f'artifact hash mismatch: {path}')
  candidates.append((key,row,ref))
 def score(item):
  key,row,ref=item
  if row['payload_sha256']==ref['payload_sha256'] and row['decoded_sha256']==ref['decoded_sha256']:
   mos=float(ref['mos']); backend=ref.get('metric_backend','zimtohrli'); source='identity:reference'
  else:
   mos,backend=score_wav_pair(ref['decoded_path'],row['decoded_path'],mode_str='audio',sample_rate=48000)
   if mos is None or backend!='zimtohrli' or not math.isfinite(float(mos)): raise RuntimeError(f'MOS failed {key}: {mos}/{backend}')
   mos=float(mos); source='scored'
  return {'key':key,'clip':row['clip'],'target_kbps':row['target_kbps'],'variant':'combo_fixed8','reference_variant':ref['variant'],'actual_payload_kbps':row['actual_payload_kbps'],'reference_payload_kbps':ref['actual_payload_kbps'],'mos':mos,'reference_mos':float(ref['mos']),'mos_delta':mos-float(ref['mos']),'payload_kbps_delta':row['actual_payload_kbps']-ref['actual_payload_kbps'],'mos_backend':backend,'mos_source':source,'payload_sha256':row['payload_sha256'],'decoded_sha256':row['decoded_sha256'],'reference_payload_sha256':ref['payload_sha256'],'reference_decoded_sha256':ref['decoded_sha256'],'encoded_sha256':row['encoded_sha256'],'decoded_file_sha256':row['decoded_file_sha256'],'reference_encoded_sha256':ref['encoded_sha256'],'reference_decoded_file_sha256':ref['decoded_sha256']}
 with ThreadPoolExecutor(max_workers=2) as pool: scored=list(pool.map(score,candidates))
 clip_names=tuple(sorted({x['clip'] for x in scored})); comparisons=[]
 for clip in clip_names:
  subset={x['target_kbps']:x for x in scored if x['clip']==clip}
  if set(subset)!=set(RATES):raise RuntimeError(f'missing rate for {clip}')
  cand=[(subset[r]['actual_payload_kbps'],subset[r]['mos']) for r in RATES]
  ref=[(subset[r]['reference_payload_kbps'],subset[r]['reference_mos']) for r in RATES]
  value=bd_rate_curve(ref,cand)
  if value is None:raise RuntimeError(f'BD-rate invalid for {clip}')
  comparisons.append({'clip':clip,'bd_rate_percent':value})
 vals=[x['bd_rate_percent'] for x in comparisons]
 summary={'schema':1,'clips':list(clip_names),'rates_kbps':list(RATES),'rows':len(scored),'reference_policy':{'32':'stack_reference','48/64/96':'all_stack'},'mean_bd_rate_percent':sum(vals)/len(vals),'median_bd_rate_percent':statistics.median(vals),'clip_bootstrap_95_ci':bootstrap_mean_ci(vals),'per_clip_bd_rate':comparisons,'per_rate':{str(r):{'mean_mos_delta':sum(x['mos_delta'] for x in scored if x['target_kbps']==r)/len(clip_names),'mean_payload_kbps_delta':sum(x['payload_kbps_delta'] for x in scored if x['target_kbps']==r)/len(clip_names),'mos_delta_bootstrap_95_ci':bootstrap_mean_ci([x['mos_delta'] for x in scored if x['target_kbps']==r]),'worst_mos_deltas':sorted([{'clip':x['clip'],'mos_delta':x['mos_delta'],'payload_kbps_delta':x['payload_kbps_delta']} for x in scored if x['target_kbps']==r],key=lambda x:x['mos_delta'])} for r in RATES},'strict_interop':{'faad3_pass':sum(bool(rows[k].get('faad_strict_pass')) for k in keys),'ffmpeg_pass':sum(bool(rows[k].get('ffmpeg_strict_pass')) for k in keys),'fdk_strict_predecoded':len(keys)},'interpretation':'Four-clip gate comparing combined fixed8 candidate with its measured combined reference; negative BD-rate favors combined fixed8. No full-corpus claim.'}
 raw_sha=sha(VALID); scorepath=ARCH/'combo-fixed8-scores.json'; summpath=ARCH/'combo-fixed8-summary.json'; atomic(scorepath,{'schema':1,'rows':scored}); atomic(summpath,summary)
 prov={'schema':1,'source_validation':{'path':str(VALID),'sha256':raw_sha},'validation_summary':{'path':str(ARCH/'summary.json'),'sha256':sha(ARCH/'summary.json')},'benchmark_commit':__import__('subprocess').check_output(['git','-C',str(BENCH),'rev-parse','HEAD'],text=True).strip(),'fdk_decoder_sha256':sha(BENCH/'bin/fdkdec'),'ffmpeg_sha256':sha(Path(__import__('shutil').which('ffmpeg'))),'helper_hashes':{'phase2_mos.py':sha(BENCH/'phase2_mos.py'),'compare_results.py':sha(BENCH/'compare_results.py'),'bd_rate.py':sha(BENCH/'scripts/bd_rate.py')},'scorer_sha256':sha(Path(__file__)),'reference_policy':summary['reference_policy'],'result_hashes':{'scores':sha(scorepath),'summary':sha(summpath)},'new_encodes_or_decodes':False}
 atomic(ARCH/'combo-fixed8-provenance.json',prov)
 print(json.dumps(summary,indent=2,sort_keys=True)); return 0
if __name__=='__main__':raise SystemExit(main())
