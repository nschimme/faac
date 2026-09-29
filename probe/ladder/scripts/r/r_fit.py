# Predict Apple sf from FAAC's own sf curve (normal FAAC, same-layout long ICS, coded-in-both bands).
import sys,json,pathlib,numpy as np
sys.path.insert(0,'/private/tmp/claude-501/faac-work/fdk-ladder/probe/ladder')
from parse_dump import parse,ICS
from line_level import long_off
g=pathlib.Path('/tmp/ladder_g');idx=json.load(open(g/'g2_index.json'))
def lay(v):return (v.win_seq,v.window_shape,v.num_groups,tuple(v.group_len))
R=[]
for ci,it in enumerate(idx):
    k=it['id'];a=parse(str(g/f'{k}_apple.dump'));n=parse(str(g/f'{k}_normal.dump'))
    for i in range(max(a)+1):
        for ch in (0,1):
            av=a.get(i+1,{}).get(ch,ICS());fv=n.get(i,{}).get(ch,ICS())
            if not(av.present and fv.present and av.win_seq!=2 and lay(av)==lay(fv)):continue
            m=min(av.max_sfb,fv.max_sfb);coded=[1<=fv.band_cb[s]<=11 for s in range(fv.max_sfb)];sf=fv.band_sf
            for s in range(m):
                if not(coded[s] and 1<=av.band_cb[s]<=11):continue
                nb=[sf[t] for t in (s-1,s+1) if 0<=t<fv.max_sfb and coded[t]]
                if not nb:continue
                nb2=[sf[t] for t in range(s-2,s+3) if t!=s and 0<=t<fv.max_sfb and coded[t]]
                R.append((ci,s,sf[s]-av.band_sf[s],sf[s]-np.mean(nb),sf[s]-np.mean(nb2),sf[s]-fv.global_gain))
R=np.array(R,dtype=float);np.save('r_rows.npy',R)
clip,sb,y,d1,d2,rel=R.T
print('n',len(y),'y mean %.2f sd %.2f'%(y.mean(),y.std()))
def r2(X):
    X=np.column_stack(X+[np.ones_like(y)]);b,*_=np.linalg.lstsq(X,y,rcond=None);p=X@b;return 1-((y-p)**2).sum()/((y-y.mean())**2).sum(),b
bands=[np.array(sb==s,float) for s in range(46)]
print('band one-hot (static tilt) R2 %.3f'%r2(bands[1:])[0])
print('d1 only R2 %.3f b=%s'%r2([d1]))
print('d2 only R2 %.3f b=%s'%r2([d2]))
print('tilt+d1 R2 %.3f'%r2(bands[1:]+[d1])[0])
print('tilt+d1*band R2 %.3f'%r2(bands[1:]+[d1*b for b in bands])[0])
print('tilt+d2+rel R2 %.3f'%r2(bands[1:]+[d2,rel])[0])
# roughness: sd of neighbour deviation in FAAC vs Apple is implied by d1 slope
