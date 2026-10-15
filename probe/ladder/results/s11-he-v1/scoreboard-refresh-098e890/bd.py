import json,sys,numpy as np,collections
sys.path.insert(0,'../fb/scripts'); sys.path.insert(0,'../fb')
from bd_rate import bd_rate_curve
rows=json.load(open('rows_faac,apple.json'))
bad=[r for r in rows if 'mos' not in r or r['mos'] is None]; print('invalid rows',len(bad))
d=collections.defaultdict(dict)
for r in rows:
    if r.get('mos') is not None: d[r['clip']].setdefault(r['arm'],[]).append((r['kbps'],r['mos']))
vals={};
for c,a in d.items():
    if 'faac' in a and 'apple' in a:
        v=bd_rate_curve(sorted(a['apple']),sorted(a['faac']))
        if v is not None: vals[c]=v
x=np.array(list(vals.values())); rng=np.random.default_rng(0)
bs=[rng.choice(x,len(x)).mean() for _ in range(10000)]
print(f'n={len(x)} mean={x.mean():.3f}% median={np.median(x):.3f}% CI95=[{np.percentile(bs,2.5):.3f},{np.percentile(bs,97.5):.3f}]')
for r in (40,48,56,64):
    f=[v['mos'] for v in rows if v['arm']=='faac' and v['rate']==r and v.get('mos')]
    a=[v['mos'] for v in rows if v['arm']=='apple' and v['rate']==r and v.get('mos')]
    fk=np.mean([v['kbps'] for v in rows if v['arm']=='faac' and v['rate']==r]); ak=np.mean([v['kbps'] for v in rows if v['arm']=='apple' and v['rate']==r])
    print(r,f'faac MOS {np.mean(f):.3f} @{fk:.1f}kbps  apple MOS {np.mean(a):.3f} @{ak:.1f}kbps')
print('worst',sorted(vals.items(),key=lambda t:-t[1])[:5])
print('best',sorted(vals.items(),key=lambda t:t[1])[:3])
json.dump(vals,open('bd_per_clip.json','w'))
