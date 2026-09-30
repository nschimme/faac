"""Characterise two HE cores side by side (decisions read from FAAD dumps of the cached encodes).
usage: x_char.py <W> <armA> <armB>   (arms from W/enc: F, Fm, X)  -> per-stream means over 49 clips
Per ICS: short share; long max_sfb; band-type shares over coded long bands (ZERO 0, regular 1-11, NOISE 13, IS 14/15);
M/S share of regular CPE bands; TNS share; zero share of lines in regular long bands; core share of AU bytes."""
import sys,os,json,pathlib,subprocess,statistics as st,numpy as np
here=pathlib.Path(__file__).resolve();sys.path.insert(0,str(here.parent));sys.path.insert(0,str(here.parents[2]))
from xs import raw_aus,write_adts,FAAD,cpe_bits
from parse_dump import parse
W=pathlib.Path(sys.argv[1]);arms=sys.argv[2:];REF=os.environ.get('X_REF')
def stats(m4a):
    au=raw_aus(m4a,W/'c.adts');a=W/'c2.adts';write_adts(au,a);d=W/'c.dump'
    subprocess.run([FAAD,'-q','-f','raw','-o',str(W/'c.raw'),str(a)],env=dict(os.environ,FAAD_DUMP=str(d),FAAD_LADDER_DUMP='1'),capture_output=True,check=True)
    cb=cpe_bits(d);fr=parse(str(d));[p.unlink(missing_ok=True) for p in (a,d,W/'c.raw')]
    n=sh=tns=0;msf=[];bt={'zero':0,'reg':0,'pns':0,'is':0};ms=msn=0;zl=zt=0
    for f in fr.values():
        for c,x in f.items():
            if not x.present:continue
            n+=1;tns+=x.tns_present
            if x.win_seq==2:sh+=1;continue
            msf.append(x.max_sfb)
            for b in range(x.num_bands):
                t=x.band_cb[b];k='zero' if t==0 else 'pns' if t==13 else 'is' if t in(14,15) else 'reg';bt[k]+=1
                if c==0 and t not in(0,13,14,15):msn+=1;ms+=x.band_ms[b]
            q=np.array(x.quantized[:1024]);zl+=np.count_nonzero(q==0);zt+=len(q)
    nb=sum(bt.values())
    return {'short':sh/n,'maxsfb':st.mean(msf) if msf else 0,**{k:v/nb for k,v in bt.items()},'ms':ms/max(msn,1),'tns':tns/n,
            'zero_lines':zl/zt,'core_bytes':sum(cb.values())/8/sum(map(len,au))}
out={}
for m in sorted((W/'enc').glob(f'{arms[0]}__*.m4a')):
    s=m.name.split('__',1)[1];out[s]={}
    for a in arms:
        p=W/'enc'/f'{a}__{s}' if not (a=='X' and REF) else pathlib.Path(REF)/s
        out[s][a]=stats(p)
json.dump(out,open(W/'char.json','w'),indent=0)
keys=list(next(iter(out.values()))[arms[0]])
print('stream '+' '.join(f'{k:>10s}' for k in keys))
for a in arms:print(f'{a:6s} '+' '.join(f"{st.mean(out[s][a][k] for s in out):10.3f}" for k in keys))
