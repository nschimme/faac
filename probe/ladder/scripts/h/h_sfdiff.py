import sys
import os,pathlib
_W=pathlib.Path(os.environ.get('LADDER_WORK','./ladder_work'));_REPO=pathlib.Path(__file__).resolve().parents[4]
LADDER_G=pathlib.Path(os.environ.get('LADDER_G',str(_W/'g')));LADDER_H=pathlib.Path(os.environ.get('LADDER_H',str(_W/'h')));LADDER_S=pathlib.Path(os.environ.get('LADDER_S',str(_W/'s')))
FAAC_BIN=os.environ.get('FAAC_BIN',str(_REPO/'build_ladder/frontend/faac'));FAAD_BIN=os.environ.get('FAAD_BIN','/tmp/faad-ladder-dump/build_faad/frontend/faad');SCORE_CLIP=os.environ.get('SCORE_CLIP','/opt/faac-benchmark/scripts/score_clip.py');PYTHON_BIN=os.environ.get('PYTHON_BIN',sys.executable)
FAAC_SMOOTH_BIN=os.environ.get('FAAC_SMOOTH_BIN','/home/user/wt/sf-smooth/build/frontend/faac');FAAC_MASTER_BIN=os.environ.get('FAAC_MASTER_BIN','/home/user/wt/master/build/frontend/faac')
import sys,json,pathlib,numpy as np,collections
sys.path.insert(0,str(_REPO/'probe/ladder'))
from parse_dump import parse,ICS
from line_level import long_off,short_off,region
g=LADDER_G;root=LADDER_H
idx=json.load(open(g/'g2_index.json'))
def lay(v):return (v.win_seq,v.window_shape,v.num_groups,tuple(v.group_len))
acc=collections.defaultdict(list)   # (scope, win, region) -> diffs  (FAAC@W − Apple)
for it in idx:
    k=it['id'];a=parse(str(g/f'{k}_apple.dump'));n=parse(str(g/f'{k}_normal.dump'));w=parse(str(root/f'{k}_W.dump'))
    for i in range(max(a)+1):
        aa=a.get(i+1,{});nn=n.get(i,{});ww=w.get(i,{})
        diff=any(lay(aa.get(c,ICS()))!=lay(nn.get(c,ICS())) for c in (0,1))
        for ch in (0,1):
            av=aa.get(ch,ICS());wv=ww.get(ch,ICS())
            if not(av.present and wv.present and lay(av)==lay(wv)):continue
            off=short_off if av.win_seq==2 else long_off;nw=128 if av.win_seq==2 else 1024
            for gi in range(av.num_groups):
                for sb in range(min(av.max_sfb,wv.max_sfb)):
                    b=gi*wv.max_sfb+sb;ab=gi*av.max_sfb+sb
                    if 1<=wv.band_cb[b]<=11 and 1<=av.band_cb[ab]<=11:
                        hz=(off[sb]+off[sb+1])*0.5*24000/nw
                        acc[('diffwin' if diff else 'samewin','short' if av.win_seq==2 else 'long',region(hz))].append(wv.band_sf[b]-av.band_sf[ab])
for key in sorted(acc):
    v=np.array(acc[key]);print(key,len(v),'mean %+.2f  |d| %.2f'%(v.mean(),np.abs(v).mean()))
