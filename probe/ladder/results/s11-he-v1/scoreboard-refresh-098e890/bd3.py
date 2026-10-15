import json,sys,numpy as np,collections
sys.path.insert(0,'../fb/scripts'); sys.path.insert(0,'../fb')
from bd_rate import bd_rate_curve
rows=json.load(open(sys.argv[1])); ref=sys.argv[2]; RUNGS=sorted({r['rate'] for r in rows})
print('invalid',sum(1 for r in rows if r.get('mos') is None))
d=collections.defaultdict(dict)
for r in rows:
    if r.get('mos') is not None: d[r['clip']].setdefault(r['arm'],[]).append((r['kbps'],r['mos']))
vals={c:bd_rate_curve(sorted(a[ref]),sorted(a['faac'])) for c,a in d.items() if 'faac' in a and ref in a}
vals={c:v for c,v in vals.items() if v is not None}
x=np.array(list(vals.values())); rng=np.random.default_rng(0); bs=[rng.choice(x,len(x)).mean() for _ in range(10000)]
print(f'FAAC vs {ref}: n={len(x)} mean={x.mean():.3f}% median={np.median(x):.3f}% CI95=[{np.percentile(bs,2.5):.3f},{np.percentile(bs,97.5):.3f}]')
for k in RUNGS:
    m=lambda a:(np.mean([v['mos'] for v in rows if v['arm']==a and v['rate']==k]),np.mean([v['kbps'] for v in rows if v['arm']==a and v['rate']==k]))
    print(k,{a:tuple(round(t,3) for t in m(a)) for a in ('faac','fdk','apple')})
json.dump(vals,open(f'bd_lc_{ref}.json','w'))
