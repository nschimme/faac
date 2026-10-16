import json,sys,numpy as np
sys.path.insert(0,'../fb/scripts'); sys.path.insert(0,'../fb')
from bd_rate import bd_rate_curve
rng=np.random.default_rng(0); R=(128,160,192)
rows=json.load(open([f for f in __import__('glob').glob('rows_lcv_*.json')][0]))
fdk={(r['arm'],r['clip'],r['rate']):r['mos_fdk'] for r in json.load(open(sys.argv[1]))} if len(sys.argv)>1 else None
K={(r['arm'],r['clip'],r['rate']):r for r in rows}; clips=sorted({r['clip'] for r in rows})
dec=[('faad2',lambda a,c,k:K[(a,c,k)]['mos']),('ffmpeg',lambda a,c,k:K[(a,c,k)]['mos_ff'])]
if fdk: dec.append(('fdkdec',lambda a,c,k:fdk[(a,c,k)]))
def ci(x):
    x=np.array(x); b=[rng.choice(x,len(x)).mean() for _ in range(10000)]; return x.mean(),np.percentile(b,2.5),np.percentile(b,97.5)
for arm in (('lcv_j1','lcv_c20') if fdk else ('lcv_nopns','lcv_j1','lcv_nopnsj1','lcv_notns','lcv_c20')):
    line=f'{arm[4:]:8s} vs default'
    for dn,f in dec:
        v=[bd_rate_curve(sorted((K[('lcv_default',c,k)]['kbps'],f('lcv_default',c,k)) for k in R),sorted((K[(arm,c,k)]['kbps'],f(arm,c,k)) for k in R)) for c in clips]
        v=[x for x in v if x is not None]; m,lo,hi=ci(v); line+=f' | {dn} {m:+6.2f}% [{lo:+.2f},{hi:+.2f}]'
    print(line)
print('mean kbps/MOS(faad2) per rung:')
for a in ('lcv_default','lcv_nopns','lcv_j1','lcv_nopnsj1','lcv_notns','lcv_c20'):
    print(' ',a[4:],[(k,round(np.mean([K[(a,c,k)]['kbps'] for c in clips]),1),round(np.mean([K[(a,c,k)]['mos'] for c in clips]),3)) for k in R])
