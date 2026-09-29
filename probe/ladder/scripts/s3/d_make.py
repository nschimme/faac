"""D oracle arms that only move the M/S bit where it is a pure stereo choice: bands coded regular (books 1-11)
in both channels of both FAAC and Apple, same window layout. (g_make's rMS also flips the bit on FAAC IS bands,
where it inverts the intensity phase, and on PNS bands; that construction is invalid.)
  rMSr    = FAAC decisions + Apple's M/S bit on those bands
  rSFMSr  = rMSr + Apple's sf on those bands (both channels)
  rSFr    = Apple's sf on those bands, FAAC's M/S bit (the rSFMSr control)
usage: d_make.py <G>"""
import sys,os,copy,json,pathlib
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[2]))
from parse_dump import parse,ICS
G=pathlib.Path(sys.argv[1])
def lay(v):return (v.win_seq,v.window_shape,v.num_groups,tuple(v.group_len))
for it in json.load(open(G/'g2_index.json')):
    k=it['id'];a=parse(str(G/f'{k}_apple.dump'));f=parse(str(G/f'{k}_normal.dump'));cnt={}
    for arm in ('rMSr','rSFMSr','rSFr'):
        nb=flip=0
        with open(G/f'{k}_G_{arm}.bin','wb') as out,open(G/f'{k}_G_{arm}_origin.bin','wb') as og:
            for idx in range(max(a)+1):
                aa=a.get(idx+1,{});ff=f.get(idx,{})
                recs=[copy.deepcopy(ff.get(c,ICS())) for c in (0,1)];org=[ICS(),ICS()]
                for c in (0,1):
                    org[c].present=recs[c].present;org[c].band_cb=[1]*128
                ok=all(c in aa and c in ff and aa[c].present and ff[c].present for c in (0,1)) and all(lay(aa[c])==lay(ff[c]) for c in (0,1))
                if ok:
                    fv=[ff[0],ff[1]];av=[aa[0],aa[1]]
                    for g in range(fv[0].num_groups):
                        for sb in range(min(fv[0].max_sfb,av[0].max_sfb,fv[1].max_sfb,av[1].max_sfb)):
                            fb=g*fv[0].max_sfb+sb;ab=g*av[0].max_sfb+sb
                            if not all(1<=v.band_cb[fb]<=11 for v in fv) or not all(1<=v.band_cb[ab]<=11 for v in av):continue
                            nb+=1;ch=False
                            if arm!='rSFr' and fv[0].band_ms[fb]!=av[0].band_ms[ab]:
                                for c in (0,1):recs[c].band_ms[fb]=av[0].band_ms[ab]
                                flip+=1;ch=True
                            if arm in ('rSFMSr','rSFr'):
                                for c in (0,1):
                                    if recs[c].band_sf[fb]!=av[c].band_sf[ab]:recs[c].band_sf[fb]=av[c].band_sf[ab];ch=True
                            if ch:
                                for c in (0,1):org[c].band_cb[fb]=0
                for c in (0,1):out.write(recs[c].pack());og.write(org[c].pack())
        cnt[arm]={'regular_bands':nb,'ms_flips':flip}
    print(k,it['stem'],cnt,flush=True)
