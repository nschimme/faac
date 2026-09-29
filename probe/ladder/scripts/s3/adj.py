"""Bits-adjusted table. usage: adj.py <json> <control> <arm>[,<arm>...]   (slope from base<lo>/base<hi>, env LADDER_SLOPE)"""
import os,sys,json,math,statistics as st
r=json.load(open(sys.argv[1]));ctl=sys.argv[2];lo,hi=('base'+x for x in os.environ.get('LADDER_SLOPE','112,144').split(','))
key=['girl','Last_Of','liberate','take_your','velvet','bas','Changes','Severance']
print(f"{'arm':12s} {'n':>3s} {'adj':>8s} {'med':>8s}  W/L  {'bytes%':>6s} {'worst':>7s} {'':10s} "+' '.join(k[:7].rjust(7) for k in key))
for arm in sys.argv[3].split(','):
    d={};b0=b1=0
    for k,v in r.items():
        if arm not in v or ctl not in v:continue
        sl=(v[hi]['mos']-v[lo]['mos'])/math.log2(v[hi]['bytes']/v[lo]['bytes'])
        d[v['stem']]=(v[arm]['mos']-v[ctl]['mos'])-sl*math.log2(v[arm]['bytes']/v[ctl]['bytes']);b0+=v[ctl]['bytes'];b1+=v[arm]['bytes']
    if not d:continue
    x=list(d.values());ws=min(d,key=d.get);kv=[next((d[s] for s in d if s.startswith(q)),float('nan')) for q in key]
    print(f"{arm:12s} {len(x):3d} {st.mean(x):+.4f} {st.median(x):+.4f} {sum(y>0.0005 for y in x):2d}/{sum(y<-0.0005 for y in x):2d} {100*(b1/b0-1):+6.2f} {d[ws]:+.3f} {ws[:10]:10s} "+' '.join(f'{y:+.3f}'.rjust(7) for y in kv))
