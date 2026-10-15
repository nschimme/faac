#!/usr/bin/env python3
"""Freeze source anchors, provenance and reviewer summary after full49 scoring."""
import hashlib,json,math,os,shutil,subprocess,sys
from pathlib import Path
ROOT=Path('/Users/nschimme/gitprojects/faac'); BENCH=Path('/Users/nschimme/gitprojects/faac-benchmark')
OUT=Path(__file__).resolve().parent; FULL=OUT/'full49'; ROWS=FULL/'rows.json'; RAW_SUM=FULL/'summary.json'
MANIFEST=Path('/private/tmp/faac-upstream098-build-manifest.json'); OLD_FDK=Path('/private/tmp/faac-he-v1-upstream098-run-provenance/fdkdec-at-capture'); NEW_FDK=Path('/private/tmp/faac-upstream098-fdk-new/fdkdec')
def sha(p):
 h=hashlib.sha256()
 with Path(p).open('rb') as f:
  for b in iter(lambda:f.read(1<<20),b''):h.update(b)
 return h.hexdigest()
def main():
 sys.path.insert(0,str(BENCH))
 import utils
 from compare_results import bootstrap_mean_ci
 from scripts.bd_rate import bd_rate_curve
 d=json.loads(ROWS.read_text()); rows=d['rows']; errors=d['errors']; rates=d['rates_kbps']; clips=d['clips']
 if len(rows)!=392 or len(clips)!=49 or errors:raise RuntimeError(f'full matrix incomplete: rows={len(rows)} clips={len(clips)} errors={len(errors)}')
 if any(not math.isfinite(float(r['mos'])) or r['profile']!='HE-AAC' for r in rows.values()):raise RuntimeError('non-finite MOS or non-HE profile')
 # Recreate and freeze the actual reference WAVs consumed by wav_conv for MOS.
 refs={}; refdir=FULL/'original-reference-48k-stereo'; refdir.mkdir(exist_ok=True)
 for clip in clips:
  original=Path(rows[f'upstream_baseline|{clip}|32']['input_path'])
  orig_sha=sha(original)
  seen={rows[f'{v}|{clip}|32']['input_sha256'] for v in ('upstream_baseline','upstream_fixed8')}
  if seen!={orig_sha}:raise RuntimeError(f'input hash drift for {clip}')
  converted=refdir/(clip+'.wav')
  if not utils.wav_conv(str(original),str(converted),rate=48000,channels=2):raise RuntimeError(f'anchor conversion failed: {clip}')
  refs[clip]={'input_path':str(original),'input_sha256':orig_sha,'converted_reference_wav_path':str(converted),'converted_reference_wav_sha256':sha(converted),'converted_sample_rate':48000,'converted_channels':2}
 # Recompute comparison summary and measured per-rate MOS deltas. Preserve raw summary as-written.
 perclip=[]
 for clip in clips:
  b=[rows[f'upstream_baseline|{clip}|{r}'] for r in rates]; c=[rows[f'upstream_fixed8|{clip}|{r}'] for r in rates]
  bd=bd_rate_curve([(x['actual_payload_kbps'],x['mos']) for x in b],[(x['actual_payload_kbps'],x['mos']) for x in c])
  if bd is None or not math.isfinite(float(bd)):raise RuntimeError(f'invalid BD-rate for {clip}')
  perclip.append({'clip':clip,'bd_rate_percent':float(bd)})
 vals=[x['bd_rate_percent'] for x in perclip]
 ci=bootstrap_mean_ci(vals,n=2000,seed=0)
 ci={k:ci[k] for k in ('mean','lo','hi','n')}
 per_rate={}
 for rate in rates:
  paired=[]
  for clip in clips:
   b=rows[f'upstream_baseline|{clip}|{rate}']; c=rows[f'upstream_fixed8|{clip}|{rate}']
   paired.append({'clip':clip,'mos_delta':c['mos']-b['mos'],'payload_kbps_delta':c['actual_payload_kbps']-b['actual_payload_kbps'],'baseline_mos':b['mos'],'fixed8_mos':c['mos'],'baseline_payload_kbps':b['actual_payload_kbps'],'fixed8_payload_kbps':c['actual_payload_kbps']})
  mci=bootstrap_mean_ci([x['mos_delta'] for x in paired],n=2000,seed=0)
  per_rate[str(rate)]={'mean_mos_delta':sum(x['mos_delta'] for x in paired)/49,'mean_payload_kbps_delta':sum(x['payload_kbps_delta'] for x in paired)/49,'mos_delta_bootstrap_95_ci':{k:mci[k] for k in ('mean','lo','hi','n')},'worst_mos_deltas':sorted(paired,key=lambda x:x['mos_delta'])[:10]}
 result={'schema':1,'comparison':'upstream_fixed8 versus upstream_baseline','clips':49,'rows':392,'rates_kbps':rates,'mean_bd_rate_percent':sum(vals)/49,'median_bd_rate_percent':__import__('statistics').median(vals),'clip_bootstrap_95_ci':ci,'strictly_negative_bd_wins':sum(x<0 for x in vals),'strictly_positive_bd_losses':sum(x>0 for x in vals),'exact_zero_bd':sum(x==0 for x in vals),'negative_bd_is_better':True,'per_clip_bd_rate':perclip,'per_rate':per_rate,'mos_anchor':'original source WAV converted to 48 kHz stereo, scored with Zimtohrli audio mode','bootstrap':{'helper':'compare_results.bootstrap_mean_ci','resamples':2000,'seed':0},'strict_interop_status':'full-matrix FAAD/FFmpeg pending; 4x4 gate had 32/32 each'}
 (FULL/'review-summary.json').write_text(json.dumps(result,indent=2,sort_keys=True)+'\n')
 ffmpeg=shutil.which('ffmpeg'); faad=Path('/private/tmp/faac-he-v1-decoder-build/frontend/faad')
 helpernames=('phase2_mos.py','utils.py','scripts/bd_rate.py','compare_results.py')
 prov={'schema':1,'run':{'started_tool':'run_full49.py','run_script_sha256':sha(OUT/'run_full49.py'),'numba_disable_jit':'1','workers':2,'original_score_session_id':63598,'full_run_exit_status':0},'source':{'manifest_path':str(MANIFEST),'manifest_sha256':sha(MANIFEST),'rows_path':str(ROWS),'rows_sha256':sha(ROWS),'raw_summary_path':str(RAW_SUM),'raw_summary_sha256':sha(RAW_SUM),'review_summary_path':str(FULL/'review-summary.json'),'review_summary_sha256':sha(FULL/'review-summary.json'),'source_commit':json.loads(MANIFEST.read_text())['upstream_commit'],'clips':refs},'benchmark':{'head_after_run':'bdce18f7123f51c31795afe9fb4571c18f507a77','helper_files':{n:sha(BENCH/n) for n in helpernames},'helper_files_unchanged_since':'adef778d6f96ea6ce24448fde962f73dc09d012f','lagbounded_bugfix_commit':'5a00e653c31e8281b24565be1f2d1ce283a98ec9','lagbounded_bugfix_in_adef778':True,'benchmark_commit_in_manifest_not_recorded_at_start':True},'decoders':{'fdk_used_path':'/Users/nschimme/gitprojects/faac-benchmark/bin/fdkdec','fdk_used_sha256':sha(OLD_FDK),'fdk_capture_path':str(OLD_FDK),'fdk_new_path':str(NEW_FDK),'fdk_new_sha256':sha(NEW_FDK) if NEW_FDK.is_file() else None,'fdk_pcm_equivalence':'pending 16-row comparison','faad_path':str(faad),'faad_sha256':sha(faad),'ffmpeg_path':ffmpeg,'ffmpeg_sha256':sha(Path(ffmpeg)),'strict_392':'pending'},'score_field_note':'Rows field mos_reference_sha256 contains the original input SHA256; the separately frozen converted_reference_wav_sha256 records the actual normalized reference WAV anchor. Both use the same source input per row.'}
 (FULL/'provenance.json').write_text(json.dumps(prov,indent=2,sort_keys=True)+'\n')
 print(json.dumps({'rows':392,'strictly_negative_bd_wins':result['strictly_negative_bd_wins'],'strictly_positive_bd_losses':result['strictly_positive_bd_losses'],'mean_bd_rate_percent':result['mean_bd_rate_percent'],'ci':ci,'review_summary_sha256':sha(FULL/'review-summary.json'),'provenance_sha256':sha(FULL/'provenance.json')},indent=2))
if __name__=='__main__':main()
