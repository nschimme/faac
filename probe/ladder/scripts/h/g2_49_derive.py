import sys
import os,pathlib
_SLOPE=tuple(int(x) for x in os.environ.get('LADDER_SLOPE','112,144').split(','));_LO,_HI=f'base{_SLOPE[0]}',f'base{_SLOPE[1]}'
_W=pathlib.Path(os.environ.get('LADDER_WORK','./ladder_work'));_REPO=pathlib.Path(__file__).resolve().parents[4]
LADDER_G=pathlib.Path(os.environ.get('LADDER_G',str(_W/'g')));LADDER_H=pathlib.Path(os.environ.get('LADDER_H',str(_W/'h')));LADDER_S=pathlib.Path(os.environ.get('LADDER_S',str(_W/'s')))
FAAC_BIN=os.environ.get('FAAC_BIN',str(_REPO/'build_ladder/frontend/faac'));FAAD_BIN=os.environ.get('FAAD_BIN','/tmp/faad-ladder-dump/build_faad/frontend/faad');SCORE_CLIP=os.environ.get('SCORE_CLIP','/opt/faac-benchmark/scripts/score_clip.py');PYTHON_BIN=os.environ.get('PYTHON_BIN',sys.executable)
FAAC_SMOOTH_BIN=os.environ.get('FAAC_SMOOTH_BIN','/home/user/wt/sf-smooth/build/frontend/faac');FAAC_MASTER_BIN=os.environ.get('FAAC_MASTER_BIN','/home/user/wt/master/build/frontend/faac')
import json,math,statistics as st
s=json.load(open(str(LADDER_G/'g2_scores.json')))
def adj(r,x,y):
    sl=(r[_HI]['mos']-r[_LO]['mos'])/math.log2(r[_HI]['bytes']/r[_LO]['bytes'])
    return (r[x]['mos']-r[y]['mos'])-sl*math.log2(r[x]['bytes']/r[y]['bytes'])
rows={}
for k,r in s.items():
    rows[k]=dict(stem=r['stem'][:22],AF=adj(r,'A','F'),fSF=adj(r,'fSF','A'),fWIN=adj(r,'fWIN','A'),rSF=adj(r,'rSF','F'),rWIN=adj(r,'rWIN','F'),AvApple=adj(r,'A','apple'),AppleF=adj(r,'apple','base128'))
for a in ('AF','fSF','fWIN','rSF','rWIN','AvApple','AppleF'):
    v=[x[a] for x in rows.values()]
    print(f"{a:8s} n={len(v)} mean {st.mean(v):+.4f} med {st.median(v):+.4f} win {sum(x>0 for x in v)} loss {sum(x<0 for x in v)}")
gap=sum(x['AF'] for x in rows.values())
for a in ('rSF','rWIN'): print(a,'share of A-F', 100*sum(x[a] for x in rows.values())/gap)
for a in ('fSF','fWIN'): print(a,'share of A-F', -100*sum(x[a] for x in rows.values())/gap)
print()
print(f"{'clip':22s} {'A-F':>7s} {'fSF':>7s} {'fWIN':>7s} {'rSF':>7s} {'rWIN':>7s}")
for k,x in sorted(rows.items(),key=lambda t:-t[1]['AF']):
    print(f"{x['stem']:22s} {x['AF']:+.3f} {x['fSF']:+.3f} {x['fWIN']:+.3f} {x['rSF']:+.3f} {x['rWIN']:+.3f}")
