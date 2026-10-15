import json,sys,numpy as np,collections
sys.path.insert(0,'../fb/scripts'); sys.path.insert(0,'../fb')
from bd_rate import bd_rate_curve
rows=json.load(open('rows_held_fix15_fix11_fix10_fix9.json'))
print('invalid',sum(1 for r in rows if r.get('mos') is None))
M={(r['arm'],r['clip'],r['rate']):r for r in rows}
clips=sorted({r['clip'] for r in rows}); rng=np.random.default_rng(0)
def ci(x):
    x=np.array(x); b=[rng.choice(x,len(x)).mean() for _ in range(10000)]; return x.mean(),np.percentile(b,2.5),np.percentile(b,97.5)
for arm in ['fix11','fix10','fix9']:
    v=[]
    for c in clips:
        a=sorted((M[('fix15',c,k)]['kbps'],M[('fix15',c,k)]['mos']) for k in (40,48,56,64))
        b=sorted((M[(arm,c,k)]['kbps'],M[(arm,c,k)]['mos']) for k in (40,48,56,64))
        x=bd_rate_curve(a,b); 
        if x is not None: v.append(x)
    m,lo,hi=ci(v); line=f'{arm:8s} BD vs control n={len(v)} {m:+.2f}% [{lo:+.2f},{hi:+.2f}] |'
    for k in (40,48,56,64):
        d=[M[(arm,c,k)]['mos']-M[('fix15',c,k)]['mos'] for c in clips]; dk=np.mean([M[(arm,c,k)]['kbps']-M[('fix15',c,k)]['kbps'] for c in clips])
        w=sum(x>0 for x in d); m2,lo2,hi2=ci(d)
        line+=f' {k}: {m2:+.3f}[{lo2:+.3f},{hi2:+.3f}] {w}/{len(d)-w} {dk:+.1f}kb |'
    print(line)
