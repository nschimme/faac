"""S6 split table. usage: x_an.py <res.json> <lo> <hi>
Every arm bits-adjusted vs F with FAAC's per-clip slope (s<lo>/s<hi>); components per prereg (positive = X side leads)."""
import sys,json,math,statistics as st
r=json.load(open(sys.argv[1]));lo,hi='s'+sys.argv[2],'s'+sys.argv[3];S=sorted(r['F'])
for a in r:assert set(r[a])==set(S),(a,len(r[a]))
def sl(s):return (r[hi][s]['mos']-r[lo][s]['mos'])/math.log2(r[hi][s]['bytes']/r[lo][s]['bytes'])
def adj(a,b):return {s:(r[a][s]['mos']-r[b][s]['mos'])-sl(s)*math.log2(r[a][s]['bytes']/r[b][s]['bytes']) for s in S}
def row(n,d,a=None,b=None,base=None):
    v=list(d.values());w=min(d,key=d.get);bst=max(d,key=d.get)
    by=f"{100*(sum(r[a][s]['bytes'] for s in S)/sum(r[b][s]['bytes'] for s in S)-1):+6.2f}" if a else '      '
    sh=f" {100*st.mean(v)/base:+5.0f}%" if base else '       '
    print(f"{n:28s} {st.mean(v):+.4f} {st.median(v):+.4f} {sum(x>0.0005 for x in v):2d}/{sum(x<-0.0005 for x in v):2d} {by}{sh}  worst {d[w]:+.3f} {w[:14]:14s} best {d[bst]:+.3f} {bst[:14]}")
    return st.mean(v)
print(f"{'':28s} {'adj':>7s} {'median':>7s}  W/L bytes%  share")
print('raw MOS:',' '.join(f"{a} {st.mean(r[a][s]['mos'] for s in S):.3f}" for a in r))
for a in ('Fm','X','FmC+XS','XC+FmS','FC+XS'):row(f'{a} - F',adj(a,'F'),a,'F')
G=row('Gm = X - Fm',adj('X','Fm'),'X','Fm')
row('s1 SBR (FAAC core held)',adj('FmC+XS','Fm'),'FmC+XS','Fm',G)
row('s2 SBR (X core held)',adj('X','XC+FmS'),'X','XC+FmS',G)
row('c1 core (X SBR held)',adj('X','FmC+XS'),'X','FmC+XS',G)
row('c2 core (FAAC SBR held)',adj('XC+FmS','Fm'),'XC+FmS','Fm',G)
row('FAAC kx31 core under X SBR',adj('FC+XS','F'),'FC+XS','F')
