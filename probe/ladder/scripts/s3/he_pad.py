"""E0.1: for input pads P, build all-Apple step1 records (Apple HE core frame n+1 -> FAAC frame n), encode FAAC HE
with step1, dump, and report the share of Apple's quantized integers reproduced (regular bands, same layout).
usage: he_pad.py <clip-stem> <pad,pad,...> [rate]"""
import os,sys,subprocess,wave,pathlib,numpy as np
here=pathlib.Path(__file__).resolve();sys.path.insert(0,str(here.parents[2]))
from parse_dump import parse,ICS
stem=sys.argv[1];pads=[int(x) for x in sys.argv[2].split(',')];rate=sys.argv[3] if len(sys.argv)>3 else '32'
W=pathlib.Path(os.environ.get('HE_WORK','/home/user/lw/he'));faac=os.environ.get('FAAC_BIN','/home/user/lw/bin/faac_probe2');faad='/tmp/faad-ladder-dump/build_faad/frontend/faad'
src=f'/opt/faac-benchmark/data/external/audio/{stem}.wav';ref=here.parents[2]/'ref'/os.environ.get('LADDER_REF','apple_he32k')/f'{stem}.m4a'
def dump(m4a,d):
    pathlib.Path(d).unlink(missing_ok=True)
    subprocess.run([faad,'-q','-o',str(W/'tmp.wav'),str(m4a)],env=dict(os.environ,FAAD_DUMP=str(d),FAAD_LADDER_DUMP='1'),capture_output=True,check=True);return parse(str(d))
ad=W/f'{stem}_apple.dump'
a=parse(str(ad)) if ad.exists() else dump(ref,ad)
def match(f,a,off):
    same=tot=0
    for i in f:
        if i+off not in a:continue
        for c in (0,1):
            x=f[i].get(c);y=a[i+off].get(c)
            if not x or not y or not x.present or not y.present:continue
            if (x.win_seq,x.num_groups,x.max_sfb)!=(y.win_seq,y.num_groups,y.max_sfb):continue
            q1=np.array(x.quantized);q2=np.array(y.quantized);m=(q2!=0)|(q1!=0);tot+=m.sum();same+=(q1[m]==q2[m]).sum()
    return same/max(tot,1),tot
for P in pads:
    with wave.open(src,'rb') as w:pr=w.getparams();pcm=w.readframes(w.getnframes())
    plus=W/f'{stem}_p{P}.wav'
    with wave.open(str(plus),'wb') as w:w.setparams(pr);w.writeframes(b'\0'*P*pr.nchannels*pr.sampwidth+pcm)
    # all-Apple records at FAAC index idx = Apple idx+1
    b=W/f'{stem}_A.bin'
    with open(b,'wb') as o:
        for idx in range(max(a)+1):
            for c in (0,1):o.write(a.get(idx+1,{}).get(c,ICS()).pack())
    enc=W/f'{stem}_KA{P}.m4a';enc.unlink(missing_ok=True)
    subprocess.run([faac,'-b',rate,'-o',str(enc),str(plus)],env=dict(os.environ,FAAC_STEP1=str(b),FAAC_STEP1_OFFSET='1'),capture_output=True,check=True)
    f=dump(enc,W/f'{stem}_KA{P}.dump')
    print(stem,'pad',P,'KA integer match (frame n vs Apple n+1):',[f'{o}:{match(f,a,o)[0]:.4f}' for o in (0,1,2)],flush=True)
    plus.unlink();enc.unlink()
