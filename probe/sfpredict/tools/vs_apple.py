import sys,json,collections,numpy as np
sys.path.insert(0,'/tmp/claude/fb'); sys.path.insert(0,'/tmp/claude/fb/scripts')
from bd_rate import bd_rate_curve
def load(p):
    d=collections.defaultdict(dict)
    for s,r,k,m in json.load(open(p)): d[s][r]=(k,m)
    return d
f=load(f'/tmp/claude/h/{sys.argv[1]}.json'); a=load('/tmp/claude/h/apple48.json')
rows=[]
for s in f:
    if s not in a: continue
    rs=[r for r in (128,160,192) if r in f[s]]
    v=bd_rate_curve([(a[s][r][0],a[s][r][1]) for r in rs],[(f[s][r][0],f[s][r][1]) for r in rs])
    rows.append((v if v is not None else float('nan'),s,[round(f[s][r][1]-a[s][r][1],3) for r in (96,128,160,192) if r in f[s]],[round(a[s][r][0]/f[s][r][0],3) for r in (96,128,160,192) if r in f[s]]))
rows.sort(key=lambda r:-(r[0] if r[0]==r[0] else -99))
print(len(rows),'mean BD faac vs apple',np.nanmean([r[0] for r in rows]))
for r in rows: print(f"{r[0]:+7.2f} {r[1][:30]:30s} dMOS {r[2]} apple/faac rate {r[3]}")
for r in (96,128,160,192):
    if r in next(iter(f.values())):
        print(r,'meanMOS faac',round(np.mean([f[s][r][1] for s in f]),4),'apple',round(np.mean([a[s][r][1] for s in f if s in a]),4),'rate apple/faac',round(np.mean([a[s][r][0]/f[s][r][0] for s in f if s in a]),3))
