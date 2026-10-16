import json,sys,collections
sys.path.insert(0,'/tmp/claude/fb'); sys.path.insert(0,'/tmp/claude/fb/scripts')
from bd_rate import bd_rate_curve
import numpy as np
def load(t):
    d=collections.defaultdict(dict)
    for s,r,k,m in json.load(open(f'/tmp/claude/h/{t}.json')): d[s][r]=(k,m)
    return d
def cmp(base,cand,seed=0):
    b,c=load(base),load(cand)
    res=[];worst=(9,None);dm=collections.defaultdict(list);db=collections.defaultdict(list)
    for s in b:
        rs=sorted(b[s])
        if s not in c or any(r not in c[s] for r in rs): continue
        v=bd_rate_curve([(b[s][r][0],b[s][r][1]) for r in rs],[(c[s][r][0],c[s][r][1]) for r in rs])
        if v is not None: res.append(v)
        for r in rs:
            d=c[s][r][1]-b[s][r][1]; dm[r].append(d); db[r].append((c[s][r][0]/b[s][r][0]-1)*100)
            if d<worst[0]: worst=(d,(s,r))
    res=np.array(res); rng=np.random.default_rng(seed)
    bs=[rng.choice(res,len(res)).mean() for _ in range(2000)]
    return dict(n=len(res),bd=res.mean(),ci=(np.percentile(bs,2.5),np.percentile(bs,97.5)),worst=worst,dmos={r:round(float(np.mean(v)),4) for r,v in dm.items()},dbr={r:round(float(np.mean(v)),2) for r,v in db.items()})
if __name__=='__main__':
    base=sys.argv[1]
    for t in sys.argv[2:]:
        r=cmp(base,t); print(t,f"BD {r['bd']:+.2f}% [{r['ci'][0]:+.2f},{r['ci'][1]:+.2f}] n={r['n']} worst {r['worst'][0]:+.3f} {r['worst'][1]} dMOS {r['dmos']} dBR% {r['dbr']}")
