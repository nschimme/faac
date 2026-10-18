import json,sys,collections,statistics as st
rate=sys.argv[1]
res=json.load(open(f'mos_{rate}.json'))
m=collections.defaultdict(dict)
for r,n,e,v in res: m[n][e]=v
sz={}
for l in open('sizes.txt'):
    t=l.split(); 
    if t[0]==rate: sz[' '.join(t[1:-2])]=(int(t[-2]),int(t[-1]))
rows=[]
for n,d in m.items():
    if d.get('faac') is None or d.get('apple') is None: continue
    f,a=sz[n]; rows.append((d['apple']-d['faac'],n,d['faac'],d['apple'],a/f))
rows.sort(reverse=True)
g=[r[0] for r in rows]
print(f'{rate}k n={len(rows)} mean gap A-F {st.mean(g):+.4f} median {st.median(g):+.4f} Apple wins {sum(x>0 for x in g)} FAAC wins {sum(x<0 for x in g)} bytes A/F mean {st.mean(r[4] for r in rows):.3f}')
for r in rows[:10]: print(f'  {r[0]:+.3f} F {r[2]:.3f} A {r[3]:.3f} bytes A/F {r[4]:.3f} {r[1]}')
print('  ...'); 
for r in rows[-4:]: print(f'  {r[0]:+.3f} F {r[2]:.3f} A {r[3]:.3f} bytes A/F {r[4]:.3f} {r[1]}')
