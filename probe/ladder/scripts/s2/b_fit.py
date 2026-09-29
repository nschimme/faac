# B1: residual of the #595 encoder's long-block sf vs Apple (same-layout, coded-in-both bands).
import sys,json,numpy as np
sys.path.insert(0,'/home/user/faac/probe/ladder')
from parse_dump import parse,ICS
from line_level import long_off
G='/home/user/ladder_work_m/g';idx=json.load(open(G+'/g2_index.json'));base=sys.argv[1] if len(sys.argv)>1 else 'smooth'
def lay(v):return (v.win_seq,v.window_shape,v.num_groups,tuple(v.group_len))
def reg(s):
    f=long_off[s]*24000/1024;return 0 if f<2000 else 1 if f<6000 else 2 if f<12000 else 3
R=[]
for ci,it in enumerate(idx):
    k=it['id'];a=parse(f'{G}/{k}_apple.dump');n=parse(f'{G}/{k}_{base}.dump')
    for i in range(max(a)+1):
        for ch in (0,1):
            av=a.get(i+1,{}).get(ch,ICS());fv=n.get(i,{}).get(ch,ICS())
            if not(av.present and fv.present and av.win_seq!=2 and lay(av)==lay(fv)):continue
            m=min(av.max_sfb,fv.max_sfb);coded=[1<=fv.band_cb[s]<=11 for s in range(fv.max_sfb)];sf=fv.band_sf
            for s in range(m):
                if not(coded[s] and 1<=av.band_cb[s]<=11):continue
                nb=[sf[t] for t in (s-1,s+1) if 0<=t<fv.max_sfb and coded[t]]
                if not nb:continue
                R.append((ci,s,reg(s),sf[s]-av.band_sf[s],sf[s]-np.mean(nb),sf[s]-fv.global_gain,fv.global_gain-av.global_gain))
R=np.array(R,float);np.save(f'/home/user/wsB/b_rows_{base}.npy',R)
clip,sb,rg,y,d1,rel,gg=R.T
print(base,'n',len(y),'y mean %.2f sd %.2f'%(y.mean(),y.std()))
for r in range(4):
    m=rg==r;print(f' region {r}: n={m.sum():7d} mean(y) {y[m].mean():+.2f} sd {y[m].std():.2f}  mean|y| {np.abs(y[m]).mean():.2f}')
def fit(cols,m=None):
    m=np.ones_like(y,bool) if m is None else m
    X=np.column_stack([c[m] for c in cols]);b,*_=np.linalg.lstsq(X,y[m],rcond=None);p=X@b;return 1-((y[m]-p)**2).sum()/((y[m]-y[m].mean())**2).sum(),b
one=np.ones_like(y)
print('d1 (no const) R2 %.3f b=%s'%fit([d1]))
print('d1+const R2 %.3f b=%s'%fit([d1,one]))
for r in range(4):
    m=rg==r;print(f' region {r}: d1 no-const R2 %.3f b=%s ; d1+rel R2 %.3f b=%s'%(*fit([d1],m),*fit([d1,rel],m)))
print('regional d1 (4 slopes, no const) R2 %.3f b=%s'%fit([d1*(rg==r) for r in range(4)]))
