"""B2: split Apple's scalefactor gain (rSFr) into level, shape and frequency region. Band set as s3/d_make.py:
bands coded regular (books 1-11) in both channels of both FAAC and Apple, same window layout (the rSFr set).
Per ICS (frame, channel) d = round(mean over the set of Apple sf - FAAC sf).
  K0        = the builder with no change (must equal KF: FAAC's own encode, PCM)
  rSFr      = Apple sf on the set (reference; same as d_make's rSFr)
  rSFlev    = FAAC sf + d on the set (FAAC's shape at Apple's per-ICS level)
  rSFshape  = Apple sf - d on the set (Apple's shape at FAAC's per-ICS level)
  rSFr0..3  = Apple sf only on set bands whose centre is in 0-2 / 2-6 / 6-12 / >12 kHz
Changed bands get origin 0, as in g_make/d_make rSF.
usage: b2_make.py <G>"""
import sys,copy,json,pathlib,os,time
here=pathlib.Path(__file__).resolve();sys.path.insert(0,str(here.parents[2]));sys.path.insert(0,str(here.parents[1]/'g'))
from parse_dump import parse,ICS
from g_make import fs
G=pathlib.Path(sys.argv[1])
ARMS=os.environ.get('LADDER_ARMS','K0,rSFr,rSFlev,rSFshape,rSFr0,rSFr1,rSFr2,rSFr3').split(',')
EDGES=(2000,6000,12000)
def reg(f):return sum(f>=e for e in EDGES)
def lay(v):return (v.win_seq,v.window_shape,v.num_groups,tuple(v.group_len))
out={}
reported=False
for it in json.load(open(G/'g2_index.json')):
    if os.environ.get('LADDER_CLIP') and it['stem']!=os.environ['LADDER_CLIP']:continue
    start=time.monotonic()
    k=it['id'];a=parse(str(G/f'{k}_apple.dump'));f=parse(str(G/f'{k}_normal.dump'))
    if all((G/f'{k}_G_{arm}.bin').exists() and (G/f'{k}_G_{arm}_origin.bin').exists() for arm in ARMS):continue
    files={arm:(open(G/f'{k}_G_{arm}.bin','wb'),open(G/f'{k}_G_{arm}_origin.bin','wb')) for arm in ARMS}
    cnt={arm:0 for arm in ARMS};nset=[0,0,0,0];lev=[]
    for idx in range(max(a)+1):
        aa=a.get(idx+1,{});ff=f.get(idx,{})
        ok=all(c in aa and c in ff and aa[c].present and ff[c].present for c in (0,1)) and all(lay(aa[c])==lay(ff[c]) for c in (0,1))
        S=[]   # (fb, ab, region) of the set
        if ok:
            fv=[ff[0],ff[1]];av=[aa[0],aa[1]]
            for g in range(fv[0].num_groups):
                for sb in range(min(fv[0].max_sfb,av[0].max_sfb,fv[1].max_sfb,av[1].max_sfb)):
                    fb=g*fv[0].max_sfb+sb;ab=g*av[0].max_sfb+sb
                    if all(1<=v.band_cb[fb]<=11 for v in fv) and all(1<=v.band_cb[ab]<=11 for v in av):
                        S.append((fb,ab,reg(fs(fv[0],sb))))
        d=[round(sum(aa[c].band_sf[ab]-ff[c].band_sf[fb] for fb,ab,_ in S)/len(S)) if S else 0 for c in (0,1)]
        if S:lev+=d;[nset.__setitem__(r,nset[r]+1) for _,_,r in S]
        for arm in ARMS:
            recs=[copy.deepcopy(ff.get(c,ICS())) for c in (0,1)];org=[ICS(),ICS()]
            for c in (0,1):org[c].present=recs[c].present;org[c].band_cb=[1]*128
            if arm!='K0':
                for fb,ab,r in S:
                    if arm.startswith('rSFr') and len(arm)==5 and int(arm[4])!=r:continue
                    for c in (0,1):
                        fsf=ff[c].band_sf[fb];asf=aa[c].band_sf[ab]
                        new=fsf+d[c] if arm=='rSFlev' else asf-d[c] if arm=='rSFshape' else asf
                        if new!=fsf:recs[c].band_sf[fb]=new;org[c].band_cb[fb]=0;cnt[arm]+=1
            fo,fg=files[arm]
            for c in (0,1):fo.write(recs[c].pack());fg.write(org[c].pack())
    for fo,fg in files.values():fo.close();fg.close()
    out[k]={'stem':it['stem'],'changed':cnt,'set_by_region':nset,'mean_level_shift':sum(lev)/max(1,len(lev))}
    print(k,it['stem'],out[k],flush=True)
    if not reported:
        elapsed=time.monotonic()-start;print(f'make first new clip {elapsed:.1f}s; 49 clips estimate {49*elapsed/60:.1f}min',flush=True);reported=True
    result=G/'s8l_make.json' if os.environ.get('LADDER_ARMS') else G/'b2_make.json'
    previous=json.loads(result.read_text()) if result.exists() else {}
    previous.update(out)
    result.write_text(json.dumps(previous,indent=0))
