import json,sys,hashlib,collections,math,statistics
from pathlib import Path
import numpy as np
from scipy.interpolate import PchipInterpolator
BENCH=Path('/Users/nschimme/gitprojects/faac-benchmark');sys.path[:0]=[str(BENCH),str(BENCH/'scripts')]
from bd_rate import bd_rate_curve
from compare_results import bootstrap_mean_ci
ROOT=Path(__file__).parent
rows=json.loads((BENCH/'comparison_results.json').read_text())
streams=json.loads((ROOT/'apple48-stream-validation.json').read_text())
validapple={(x['row_key'],x['scenario'],x['filename']) for x in streams if len(x['stream'])==1 and x['stream'][0].get('sample_rate')=='48000' and x['stream'][0].get('channels')==2 and x['stream'][0].get('profile') in ('LC','HE-AAC')}
D={(r['tool'],r['profile'],r['scenario'],r['filename']):r for r in rows if r['decode_valid'] and r['mos'] is not None and math.isfinite(r['mos']) and r['actual_bitrate']>0 and (not r['tool'].startswith('Apple') or (r['row_key'],r['scenario'],r['filename']) in validapple)}
f='FAAC 3.0.0 (367cf8dc)';res={}
for profile,rates in [('he',[32,40,48,56,64,96]),('lc',[96,128,160,192,256,320])]:
 for ref in ['fdkaac 1.0.9','Apple AAC 27.0.1']:
  out={};bd=[]
  for rate in rates:
   pairs=[]
   for clip in sorted({r['filename'] for r in rows if r['scenario']==f'48k_stereo_{rate}k'}):
    a=D.get((f,profile,f'48k_stereo_{rate}k',clip));b=D.get((ref,profile,f'48k_stereo_{rate}k',clip))
    if not a or not b:continue
    curve=sorted((v['actual_bitrate'],v['mos']) for k,v in D.items() if k[0]==ref and k[1]==profile and k[3]==clip and k[2] in [f'48k_stereo_{x}k' for x in rates])
    if len(curve)<2:continue
    xs,ys=zip(*curve)
    if not xs[0]<=a['actual_bitrate']<=xs[-1]:continue
    gap=float(PchipInterpolator(xs,ys)(a['actual_bitrate']))-a['mos']
    pairs.append({'clip':clip,'reference_minus_faac_equal_rate_mos':gap,'raw_mos_gap':b['mos']-a['mos'],'faac_kbps':a['actual_bitrate'],'reference_kbps':b['actual_bitrate']})
   vals=[x['reference_minus_faac_equal_rate_mos'] for x in pairs]
   if vals:out[str(rate)]={'n':len(vals),'equal_rate_mos_gap':statistics.mean(vals),'ci':bootstrap_mean_ci(vals,n=2000,seed=0),'raw_gap_matched':statistics.mean(x['raw_mos_gap'] for x in pairs),'faac_kbps':statistics.mean(x['faac_kbps'] for x in pairs),'ref_kbps':statistics.mean(x['reference_kbps'] for x in pairs),'worst':sorted(pairs,key=lambda x:x['reference_minus_faac_equal_rate_mos'],reverse=True)[:5],'pairs':pairs}
  brates=[40,48,56,64] if profile=='he' else [128,160,192,256,320]
  for clip in sorted({r['filename'] for r in rows if r['scenario']=='48k_stereo_48k'}):
   aa=[D.get((f,profile,f'48k_stereo_{rate}k',clip)) for rate in brates];bb=[D.get((ref,profile,f'48k_stereo_{rate}k',clip)) for rate in brates]
   if any(x is None for x in aa+bb):continue
   v=bd_rate_curve([(x['actual_bitrate'],x['mos']) for x in bb],[(x['actual_bitrate'],x['mos']) for x in aa])
   if v is not None and math.isfinite(v):bd.append({'clip':clip,'faac_bd_percent':v})
  vals=[x['faac_bd_percent'] for x in bd]
  res[profile+' vs '+ref]={'per_rate':out,'bd_rates':brates,'bd_n':len(vals),'faac_bd_percent':statistics.mean(vals) if vals else None,'bd_ci':bootstrap_mean_ci(vals,n=2000,seed=0) if vals else None,'bd_rows':bd}
(ROOT/'equal-rate-gaps.json').write_text(json.dumps(res,indent=2))
for name,r in res.items():
 print(name,'BD',r['bd_n'],r['faac_bd_percent'],r['bd_ci'])
 for k,v in r['per_rate'].items():print(k,v['n'],round(v['equal_rate_mos_gap'],5),{z:round(v['ci'][z],5) for z in ('lo','hi')},'worst',[(x['clip'],round(x['reference_minus_faac_equal_rate_mos'],4)) for x in v['worst'][:2]])
print('Apple FS exclusions',dict(collections.Counter((x['scenario'],tuple((s.get('sample_rate'),s.get('channels'),s.get('profile')) for s in x['stream'])) for x in streams)))
