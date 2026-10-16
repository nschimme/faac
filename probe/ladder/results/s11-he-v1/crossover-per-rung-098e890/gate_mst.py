import json,sys,glob,numpy as np
sys.path.insert(0,'../fb/scripts'); sys.path.insert(0,'../fb')
from bd_rate import bd_rate_curve
rng=np.random.default_rng(0); R=(40,48,56,64)
for tag in ('mst48','mst441'):
    fs=glob.glob(f'rows_{tag}_*.json')
    if not fs: print(tag,'not ready'); continue
    rows=json.load(open(fs[0])); M={(r['arm'],r['clip'],r['rate']):r for r in rows}; clips=sorted({r['clip'] for r in rows})
    for dn in ('mos','mos_ff'):
        d=[(M[('prm',c,k)][dn]-M[('mst',c,k)][dn],c,k) for c in clips for k in R]
        bad=[x for x in d if x[0]<-0.10]
        v=[bd_rate_curve(sorted((M[('mst',c,k)]['kbps'],M[('mst',c,k)][dn]) for k in R),sorted((M[('prm',c,k)]['kbps'],M[('prm',c,k)][dn]) for k in R)) for c in clips]
        v=np.array([x for x in v if x is not None]); b=[rng.choice(v,len(v)).mean() for _ in range(10000)]
        print(f'{tag} {dn:6s} mean dMOS {np.mean([x for x,_,_ in d]):+.4f} | cells<-0.10: {len(bad)}/{len(d)} | worst {[(round(x,3),c[:14],k) for x,c,k in sorted(d)[:3]]} | BD {v.mean():+.2f}% [{np.percentile(b,2.5):+.2f},{np.percentile(b,97.5):+.2f}]')
