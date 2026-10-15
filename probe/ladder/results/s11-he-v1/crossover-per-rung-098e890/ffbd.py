import json,sys,numpy as np
sys.path.insert(0,'../fb/scripts'); sys.path.insert(0,'../fb')
from bd_rate import bd_rate_curve
kind=sys.argv[1]
src='rows_fix15_fix13_fix11_fix9_fix7_nofix11_nofix7.json' if kind=='music' else 'rows_held_fix15_fix11_fix10_fix9.json'
K={(r['arm'],r['clip'],r['rate']):r['kbps'] for r in json.load(open(src))}
F={(r['arm'],r['clip'],r['rate']):r['mos'] for r in json.load(open(f'ff_{kind}.json'))}
clips=sorted({k[1] for k in F}); rng=np.random.default_rng(0)
def ci(x):
    x=np.array(x); b=[rng.choice(x,len(x)).mean() for _ in range(10000)]; return x.mean(),np.percentile(b,2.5),np.percentile(b,97.5)
for arm in ('fix11','fix9'):
    v=[]
    for c in clips:
        a=sorted((K[('fix15',c,k)],F[('fix15',c,k)]) for k in (40,48,56,64)); b=sorted((K[(arm,c,k)],F[(arm,c,k)]) for k in (40,48,56,64))
        x=bd_rate_curve(a,b)
        if x is not None: v.append(x)
    m,lo,hi=ci(v); line=f'[ffmpeg decode, {kind}] {arm} BD vs control n={len(v)} {m:+.2f}% [{lo:+.2f},{hi:+.2f}] |'
    for k in (40,48,56,64):
        d=[F[(arm,c,k)]-F[('fix15',c,k)] for c in clips]; m2,lo2,hi2=ci(d); line+=f' {k}: {m2:+.3f}[{lo2:+.3f},{hi2:+.3f}] {sum(x>0 for x in d)}/{sum(x<=0 for x in d)} |'
    print(line)
