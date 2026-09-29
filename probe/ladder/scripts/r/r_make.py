import sys
import os,pathlib
_W=pathlib.Path(os.environ.get('LADDER_WORK','./ladder_work'));_REPO=pathlib.Path(__file__).resolve().parents[4]
LADDER_G=pathlib.Path(os.environ.get('LADDER_G',str(_W/'g')));LADDER_H=pathlib.Path(os.environ.get('LADDER_H',str(_W/'h')));LADDER_S=pathlib.Path(os.environ.get('LADDER_S',str(_W/'s')))
FAAC_BIN=os.environ.get('FAAC_BIN',str(_REPO/'build_ladder/frontend/faac'));FAAD_BIN=os.environ.get('FAAD_BIN','/tmp/faad-ladder-dump/build_faad/frontend/faad');SCORE_CLIP=os.environ.get('SCORE_CLIP','/opt/faac-benchmark/scripts/score_clip.py');PYTHON_BIN=os.environ.get('PYTHON_BIN',sys.executable)
FAAC_SMOOTH_BIN=os.environ.get('FAAC_SMOOTH_BIN','/home/user/wt/sf-smooth/build/frontend/faac');FAAC_MASTER_BIN=os.environ.get('FAAC_MASTER_BIN','/home/user/wt/master/build/frontend/faac')
# Rule arms: normal FAAC decisions re-emitted via step1 with only long-block sf rewritten.
# Coefficients fit on the other half of the clips (even/odd split) from r_rows.npy.
import sys,copy,json,pathlib,numpy as np
sys.path.insert(0,str(_REPO/'probe/ladder'))
from parse_dump import parse,ICS
g=LADDER_G;root=LADDER_H;idx=json.load(open(g/'g2_index.json'))
R=np.load(root/'r_rows.npy');clip,sb,y,d1,d2,rel=R.T
ARMS={'T':('tilt',),'D':('d1',),'TD':('tilt','d1'),'TDR':('tilt','d2','rel')}
FIXED={'S3':(0.3,0.0),'S6':(0.6,0.0),'S9':(0.9,0.0),'C':(0.0,None)}  # (alpha, const); const None = fitted D intercept only
def design(sbv,d1v,d2v,relv,feats):
    cols=[]
    if 'tilt' in feats:cols+=[(sbv==s).astype(float) for s in range(46)]
    else:cols+=[np.ones_like(sbv)]
    for f,v in (('d1',d1v),('d2',d2v),('rel',relv)):
        if f in feats:cols.append(v)
    return np.column_stack(cols)
coef={}
for arm,feats in ARMS.items():
    for half in (0,1):
        m=(clip%2)!=half   # fit on the OTHER half
        X=design(sb[m],d1[m],d2[m],rel[m],feats);b,*_=np.linalg.lstsq(X,y[m],rcond=None);coef[(arm,half)]=b
def make(ci,k,arms=None):
    n=parse(str(g/f'{k}_normal.dump'));half=ci%2;units={}
    for arm,feats in [(a,f) for a,f in list(ARMS.items())+[(a,('fixed',)) for a in FIXED]+[('K0',())] if arms is None or a in arms]:
        nb=0
        with open(root/f'{k}_R_{arm}.bin','wb') as out,open(root/f'{k}_R_{arm}_origin.bin','wb') as orig:
            for i in range(max(n)+1):
                for ch in (0,1):
                    fv=n.get(i,{}).get(ch,ICS());x=copy.deepcopy(fv);o=ICS();o.present=x.present
                    for b in range(128):o.band_cb[b]=1
                    if arm!='K0' and fv.present and fv.win_seq!=2:
                        coded=[1<=fv.band_cb[s]<=11 for s in range(fv.max_sfb)];sf=fv.band_sf
                        for s in range(fv.max_sfb):
                            if not coded[s]:continue
                            nb1=[sf[t] for t in (s-1,s+1) if 0<=t<fv.max_sfb and coded[t]]
                            if not nb1:continue
                            nb2=[sf[t] for t in range(s-2,s+3) if t!=s and 0<=t<fv.max_sfb and coded[t]]
                            if arm in FIXED:
                                al,c=FIXED[arm];c=coef[('D',half)][0] if c is None else c;pred=al*(sf[s]-np.mean(nb1))+c
                            else:
                             X=design(np.array([float(min(s,45))]),np.array([sf[s]-np.mean(nb1)]),np.array([sf[s]-np.mean(nb2)]),np.array([float(sf[s]-fv.global_gain)]),feats)
                             pred=float((X@coef[(arm,half)])[0])   # predicted FAAC−Apple
                            new=int(round(sf[s]-pred));new=max(0,min(255,new))
                            if new!=sf[s]:x.band_sf[s]=new;nb+=1
                        # keep sf deltas legal (|Δ|≤60 between consecutive coded bands)
                        prev=None
                        for s in range(fv.max_sfb):
                            if coded[s]:
                                if prev is not None:x.band_sf[s]=max(prev-60,min(prev+60,x.band_sf[s]))
                                prev=x.band_sf[s]
                    out.write(x.pack());orig.write(o.pack())
        units[arm]=nb
    return units
