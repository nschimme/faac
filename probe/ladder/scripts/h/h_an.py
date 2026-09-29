import sys
import os,pathlib
_W=pathlib.Path(os.environ.get('LADDER_WORK','./ladder_work'));_REPO=pathlib.Path(__file__).resolve().parents[4]
LADDER_G=pathlib.Path(os.environ.get('LADDER_G',str(_W/'g')));LADDER_H=pathlib.Path(os.environ.get('LADDER_H',str(_W/'h')));LADDER_S=pathlib.Path(os.environ.get('LADDER_S',str(_W/'s')))
FAAC_BIN=os.environ.get('FAAC_BIN',str(_REPO/'build_ladder/frontend/faac'));FAAD_BIN=os.environ.get('FAAD_BIN','/tmp/faad-ladder-dump/build_faad/frontend/faad');SCORE_CLIP=os.environ.get('SCORE_CLIP','/opt/faac-benchmark/scripts/score_clip.py');PYTHON_BIN=os.environ.get('PYTHON_BIN',sys.executable)
FAAC_SMOOTH_BIN=os.environ.get('FAAC_SMOOTH_BIN','/home/user/wt/sf-smooth/build/frontend/faac');FAAC_MASTER_BIN=os.environ.get('FAAC_MASTER_BIN','/home/user/wt/master/build/frontend/faac')
import json,math,statistics as st,os
g=json.load(open(str(LADDER_G/'g2_scores.json')));h=json.load(open(str(LADDER_H/'h_scores.json')))
h2=json.load(open(str(LADDER_H/'h2_scores.json'))) if os.path.exists(str(LADDER_H/'h2_scores.json')) else {}
def sl(r):return (r['base144']['mos']-r['base112']['mos'])/math.log2(r['base144']['bytes']/r['base112']['bytes'])
def adj(r,x,rb,y):return (x['mos']-y['mos'])-sl(r)*math.log2(x['bytes']/y['bytes'])
rows=[]
for k,r in g.items():
    N=h[k]['N'];F=r['F']
    d={'stem':r['stem'][:20],'AF':adj(r,r['A'],r,F),'rWIN':adj(r,r['rWIN'],r,F),'rSF':adj(r,r['rSF'],r,F),'NF':N['mos']-F['mos']}
    for a in ('W','S','WS','WSC','WSCM'):d[a]=adj(r,h[k][a],r,N)
    if k in h2:
        W=h[k]['W']
        for a in ('WaSFd','WaSF'):d[a]=adj(r,h2[k][a],r,N);d[a+'_vW']=adj(r,h2[k][a],r,W)
    rows.append(d)
print('N-F mos mean',st.mean(x['NF'] for x in rows))
keys=['AF','rWIN','rSF','W','S','WS','WSC','WSCM']+[a for a in ('WaSFd','WaSF','WaSFd_vW') if a in rows[0]]
for sel,lab in ((lambda x:True,'all49'),(lambda x:x['rWIN']>0.03,'window-heavy (rWIN>0.03)'),(lambda x:abs(x['rWIN'])<0.01,'clean-window (|rWIN|<0.01)')):
    R=[x for x in rows if sel(x)];print(f'== {lab} n={len(R)}')
    for a in keys:
        v=[x[a] for x in R if a in x]
        if v:print(f'  {a:9s} mean {st.mean(v):+.4f} med {st.median(v):+.4f} W/L {sum(z>0 for z in v)}/{sum(z<0 for z in v)}')
print(f"{'clip':20s} "+' '.join(f'{a:>7s}' for a in keys))
for x in sorted(rows,key=lambda x:-x['rWIN'])[:14]+[x for x in rows if x['stem'].startswith('girl')]:
    print(f"{x['stem']:20s} "+' '.join(f"{x.get(a,float('nan')):+.3f}".rjust(7) for a in keys))
