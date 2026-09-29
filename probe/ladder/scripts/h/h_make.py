import sys
import os,pathlib
_W=pathlib.Path(os.environ.get('LADDER_WORK','./ladder_work'));_REPO=pathlib.Path(__file__).resolve().parents[4]
LADDER_G=pathlib.Path(os.environ.get('LADDER_G',str(_W/'g')));LADDER_H=pathlib.Path(os.environ.get('LADDER_H',str(_W/'h')));LADDER_S=pathlib.Path(os.environ.get('LADDER_S',str(_W/'s')))
FAAC_BIN=os.environ.get('FAAC_BIN',str(_REPO/'build_ladder/frontend/faac'));FAAD_BIN=os.environ.get('FAAD_BIN','/tmp/faad-ladder-dump/build_faad/frontend/faad');SCORE_CLIP=os.environ.get('SCORE_CLIP','/opt/faac-benchmark/scripts/score_clip.py');PYTHON_BIN=os.environ.get('PYTHON_BIN',sys.executable)
FAAC_SMOOTH_BIN=os.environ.get('FAAC_SMOOTH_BIN','/home/user/wt/sf-smooth/build/frontend/faac');FAAC_MASTER_BIN=os.environ.get('FAAC_MASTER_BIN','/home/user/wt/master/build/frontend/faac')
# rWIN-b: FAAC's own decisions made at Apple's windows (W arm), with Apple's absolute sf swapped in.
#   KW    = W dump re-emitted unchanged (control: must equal the W stream)
#   WaSFd = Apple sf on coded-in-both bands, only in ICS where Apple's window differs from normal FAAC's
#   WaSF  = Apple sf on coded-in-both bands, every ICS (includes the clean-window rSF effect)
import sys,copy,pathlib,json
sys.path.insert(0,str(_REPO/'probe/ladder'))
from parse_dump import parse,ICS
root=LADDER_H;g=LADDER_G
def lay(v):return (v.win_seq,v.window_shape,v.num_groups,tuple(v.group_len))
def make(k):
    a=parse(str(g/f'{k}_apple.dump'));n=parse(str(g/f'{k}_normal.dump'));w=parse(str(root/f'{k}_W.dump'))
    units={}
    for arm in ('KW','WaSFd','WaSF'):
        nb=nics=0
        with open(root/f'{k}_H_{arm}.bin','wb') as out,open(root/f'{k}_H_{arm}_origin.bin','wb') as orig:
            for idx in range(max(a)+1):
                aa=a.get(idx+1,{});nn=n.get(idx,{});ww=w.get(idx,{})
                diff=any(lay(aa.get(c,ICS()))!=lay(nn.get(c,ICS())) for c in (0,1))
                for ch in (0,1):
                    av=aa.get(ch,ICS());wv=ww.get(ch,ICS());x=copy.deepcopy(wv);o=ICS();o.present=x.present
                    for b in range(128):o.band_cb[b]=1
                    if arm!='KW' and av.present and wv.present and lay(av)==lay(wv) and (arm=='WaSF' or diff):
                        hit=0
                        for gi in range(x.num_groups):
                            for sb in range(min(x.max_sfb,av.max_sfb)):
                                b=gi*x.max_sfb+sb;ab=gi*av.max_sfb+sb
                                if 1<=x.band_cb[b]<=11 and 1<=av.band_cb[ab]<=11 and x.band_sf[b]!=av.band_sf[ab]:
                                    x.band_sf[b]=av.band_sf[ab];o.band_cb[b]=0;nb+=1;hit=1
                        nics+=hit
                    out.write(x.pack());orig.write(o.pack())
        units[arm]={'ics':nics,'bands':nb}
    return units
if __name__=='__main__':print(make(sys.argv[1]))
