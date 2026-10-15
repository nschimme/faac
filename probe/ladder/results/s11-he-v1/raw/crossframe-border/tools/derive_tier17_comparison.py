#!/usr/bin/env python3
"""Compare the measured persistent border policy with the measured tier17 policy."""
from __future__ import annotations
import hashlib, json, statistics, sys
from pathlib import Path

ROOT=Path('/Users/nschimme/gitprojects/faac')
BENCH=Path('/Users/nschimme/gitprojects/faac-benchmark')
PERSIST=ROOT/'.cache/he-v1-feature-study/crossframe-border/full-x2-future0-lam003/rows.json'
TIER=ROOT/'.cache/he-v1-feature-study/envelope-count/full-active-lambda/envcount-results.json'
OUT=ROOT/'.cache/he-v1-feature-study/crossframe-border/full-x2-future0-lam003'
RATES=(32,48,64,96)

def sha(path):
 h=hashlib.sha256()
 with Path(path).open('rb') as f:
  for block in iter(lambda:f.read(1<<20),b''): h.update(block)
 return h.hexdigest()

def main():
 sys.path[:0]=[str(BENCH),str(BENCH/'scripts')]
 from bd_rate import bd_rate_curve
 from compare_results import bootstrap_mean_ci
 pr=json.load(open(PERSIST))['rows']; tr=json.load(open(TIER))['rows']
 clips=tuple(sorted({key.split('|')[1] for key in pr if key.startswith('x2_f0_lam0003_')}))
 if len(clips)!=49: raise RuntimeError(f'expected 49 clips; got {len(clips)}')
 comparisons={}
 for ctx in ('base','stack'):
  pkey=f'x2_f0_lam0003_{ctx}'
  tkeys=('baseline_original','all_base','all_base','all_base') if ctx=='base' else ('stack_reference','all_stack','all_stack','all_stack')
  per=[]; vals=[]
  for clip in clips:
   persistent=[(pr[f'{pkey}|{clip}|{r}']['actual_payload_kbps'],pr[f'{pkey}|{clip}|{r}']['mos']) for r in RATES]
   tier=[(tr[f'{k}|{clip}|{r}']['actual_payload_kbps'],tr[f'{k}|{clip}|{r}']['mos']) for k,r in zip(tkeys,RATES)]
   value=bd_rate_curve(tier,persistent)
   if value is None: raise RuntimeError(f'invalid BD-rate for {ctx}/{clip}')
   vals.append(value); per.append({'clip':clip,'persistent_vs_tier17_bd_rate_percent':value})
  comparisons[ctx]={'persistent_variant':pkey,'tier17_policy_by_rate':dict(zip(map(str,RATES),tkeys)),
   'mean_bd_rate_percent':sum(vals)/len(vals),'median_bd_rate_percent':statistics.median(vals),
   'clip_bootstrap_95_ci':bootstrap_mean_ci(vals),'worst_clip':max(per,key=lambda x:x['persistent_vs_tier17_bd_rate_percent']),
   'best_clip':min(per,key=lambda x:x['persistent_vs_tier17_bd_rate_percent']),'per_clip':per}
 result={'schema':1,'clip_count':len(clips),'interpretation':'Direct within-corpus paired PCHIP BD-rate: persistent mode2/future0/lambda=.003 compared with measured tier17 envelope-count policy. Base policy uses baseline_original at 32 and all_base at 48/64/96; stack policy uses stack_reference at 32 and all_stack at 48/64/96. Negative favors persistent. This compares alternatives and is not a combined-feature gain estimate.','comparisons':comparisons}
 out=OUT/'tier17-policy-comparison-derived.json'; out.write_text(json.dumps(result,indent=2,sort_keys=True)+'\n')
 script=Path(__file__).resolve()
 prov={'schema':1,'command':'NUMBA_DISABLE_JIT=1 ../faac-benchmark/.venv/bin/python .cache/he-v1-feature-study/crossframe-border/tools/derive_tier17_comparison.py',
  'persistent_rows':{'path':str(PERSIST),'sha256':sha(PERSIST)},'persistent_config':{'path':str(OUT/'config.json'),'sha256':sha(OUT/'config.json')},
  'tier17_rows':{'path':str(TIER),'sha256':sha(TIER)},'tier17_config':{'path':str(ROOT/'.cache/he-v1-feature-study/envelope-count/full-active-lambda/envcount-config.json'),'sha256':sha(ROOT/'.cache/he-v1-feature-study/envelope-count/full-active-lambda/envcount-config.json')},
  'script':{'path':str(script),'sha256':sha(script)},'helpers':{'compare_results.py':sha(BENCH/'compare_results.py'),'bd_rate.py':sha(BENCH/'scripts/bd_rate.py')},'result':{'path':str(out),'sha256':sha(out)},'measured_rows_modified':False}
 (OUT/'tier17-policy-provenance.json').write_text(json.dumps(prov,indent=2,sort_keys=True)+'\n')
 print(json.dumps({k:{'mean':v['mean_bd_rate_percent'],'ci':v['clip_bootstrap_95_ci']} for k,v in comparisons.items()},indent=2))
if __name__=='__main__': main()
