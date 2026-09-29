import sys
import os,pathlib
_W=pathlib.Path(os.environ.get('LADDER_WORK','./ladder_work'));_REPO=pathlib.Path(__file__).resolve().parents[4]
LADDER_G=pathlib.Path(os.environ.get('LADDER_G',str(_W/'g')));LADDER_H=pathlib.Path(os.environ.get('LADDER_H',str(_W/'h')));LADDER_S=pathlib.Path(os.environ.get('LADDER_S',str(_W/'s')))
FAAC_BIN=os.environ.get('FAAC_BIN',str(_REPO/'build_ladder/frontend/faac'));FAAD_BIN=os.environ.get('FAAD_BIN','/tmp/faad-ladder-dump/build_faad/frontend/faad');SCORE_CLIP=os.environ.get('SCORE_CLIP','/opt/faac-benchmark/scripts/score_clip.py');PYTHON_BIN=os.environ.get('PYTHON_BIN',sys.executable)
FAAC_SMOOTH_BIN=os.environ.get('FAAC_SMOOTH_BIN','/home/user/wt/sf-smooth/build/frontend/faac');FAAC_MASTER_BIN=os.environ.get('FAAC_MASTER_BIN','/home/user/wt/master/build/frontend/faac')
import json,math,statistics as st
g=json.load(open(str(LADDER_G/'g2_scores.json')));h=json.load(open(str(LADDER_H/'h_scores.json')));w=json.load(open(str(LADDER_H/'wr_scores.json')))
def sl(r):return (r['base144']['mos']-r['base112']['mos'])/math.log2(r['base144']['bytes']/r['base112']['bytes'])
def adj(r,x,y):return (x['mos']-y['mos'])-sl(r)*math.log2(x['bytes']/y['bytes'])
rows=[]
for k,r in g.items():
    N=h[k]['N'];W=h[k]['W'];NR=w[k]['NR'];WR=w[k]['WR']
    rows.append({'stem':r['stem'][:22],'rWIN':adj(r,r['rWIN'],r['F']),'NR-N':adj(r,NR,N),'W-N':adj(r,W,N),'WR-N':adj(r,WR,N),'WR-NR':adj(r,WR,NR)})
keys=['NR-N','W-N','WR-N','WR-NR']
for sel,lab in ((lambda x:True,'all49'),(lambda x:x['rWIN']>0.03,'window-heavy'),(lambda x:abs(x['rWIN'])<0.01,'clean-window')):
    R=[x for x in rows if sel(x)];print(f'== {lab} n={len(R)}')
    for a in keys:
        v=[x[a] for x in R];print(f'  {a:6s} mean {st.mean(v):+.4f} med {st.median(v):+.4f} W/L {sum(z>0 for z in v)}/{sum(z<0 for z in v)}')
print(f"{'clip':22s} {'rWIN':>7s} {'W-N':>7s} {'WR-NR':>7s} {'NR-N':>7s}")
for x in sorted(rows,key=lambda x:-x['rWIN'])[:14]+[x for x in rows if x['stem'].startswith(('girl.16b48k','Last_Of'))]:
    print(f"{x['stem']:22s} {x['rWIN']:+7.3f} {x['W-N']:+7.3f} {x['WR-NR']:+7.3f} {x['NR-N']:+7.3f}")
