import json,sys,numpy as np
sys.path.insert(0,'../fb/scripts'); sys.path.insert(0,'../fb')
from bd_rate import bd_rate_curve
rng=np.random.default_rng(0)
def ci(x):
    x=np.array(x); b=[rng.choice(x,len(x)).mean() for _ in range(10000)]; return x.mean(),np.percentile(b,2.5),np.percentile(b,97.5)
rows=json.load(open('rows_x2_x2c12_x2c13_x2c11_x2c10_x2c9_x2n12_x2n9.json'))
fdk={(r['arm'],r['clip'],r['rate']):r['mos_fdk'] for r in json.load(open(sys.argv[1]))} if len(sys.argv)>1 else None
K={(r['arm'],r['clip'],r['rate']):r for r in rows}; clips=sorted({r['clip'] for r in rows}); R=(40,48,56,64)
dec=[('faad2',lambda a,c,k:K[(a,c,k)]['mos']),('ffmpeg',lambda a,c,k:K[(a,c,k)]['mos_ff'])]
if fdk: dec.append(('fdkdec',lambda a,c,k:fdk[(a,c,k)]))
def bd(arm,ref,f):
    v=[]
    for c in clips:
        a=sorted((K[(ref,c,k)]['kbps'],f(ref,c,k)) for k in R); b=sorted((K[(arm,c,k)]['kbps'],f(arm,c,k)) for k in R)
        x=bd_rate_curve(a,b)
        if x is not None: v.append(x)
    return ci(v),len(v)
for ref,arms in (('x2c12',['x2c13','x2c11','x2c10','x2c9','x2n12','x2n9']),('x2c9',['x2n9']),('x2c12',[])):
    for arm in arms:
        line=f'{arm:6s} vs {ref:6s}'
        for dn,f in dec:
            (m,lo,hi),n=bd(arm,ref,f); line+=f' | {dn} {m:+6.2f}% [{lo:+.2f},{hi:+.2f}] n={n}'
        print(line)
