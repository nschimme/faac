"""Characterise SBR payloads (FAAD dump F records) of HE streams, 49-clip means.
usage: x_sbr.py <W> <arm>... ; arm X reads W/enc/X__* or, with X_REF, the Apple ref dir.
Per SBR channel-frame: frame class share (0 FIXFIX..3 VARVAR), envelopes L_E, noise floors L_Q, share of high freq_res
envelopes, amp_res 1.5 dB share, inverse-filter mode shares (0 off..3 strong), add_harmonic share, coupling share,
mean envelope / noise values as printed by the dump (dB-like)."""
import sys,os,json,pathlib,subprocess,statistics as st,collections
here=pathlib.Path(__file__).resolve();sys.path.insert(0,str(here.parent))
from xs import raw_aus,write_adts,FAAD
W=pathlib.Path(sys.argv[1]);arms=sys.argv[2:];REF=os.environ.get('X_REF')
def stats(m4a):
    au=raw_aus(m4a,W/'s.adts');a=W/'s2.adts';write_adts(au,a);d=W/'s.dump'
    subprocess.run([FAAD,'-q','-f','raw','-o',str(W/'s.raw'),str(a)],env=dict(os.environ,FAAD_DUMP=str(d),FAAD_LADDER_DUMP='1'),capture_output=True,check=True)
    c=collections.Counter();n=0;inv=collections.Counter();ni=0;fr=[0,0];E=[];Q=[]
    for l in open(d):
        if not l.startswith('F '):continue
        t=l.split();n+=1;c['class%s'%t[3]]+=1;c['L_E']+=int(t[4]);c['L_Q']+=int(t[5])
        for x in t[6].split(','):fr[int(x)]+=1
        c['amp1.5']+=int(t[7])==0
        for x in t[8].split(','):inv[x]+=1;ni+=1
        c['harm']+=int(t[9]);c['coupl']+=int(t[11]);E.append(float(t[13]));Q.append(float(t[14]))
    [p.unlink(missing_ok=True) for p in (a,d,W/'s.raw')]
    r={k:v/n for k,v in c.items()};r.update({f'inv{k}':v/ni for k,v in inv.items()});r['hi_res']=fr[1]/max(sum(fr),1)
    r['E']=st.mean(E);r['Q']=st.mean(Q);return r
out={}
for m in sorted((W/'enc').glob('F__*.m4a')):
    s=m.name.split('__',1)[1];out[s]={a:stats(pathlib.Path(REF)/s if a=='X' and REF else W/'enc'/f'{a}__{s}') for a in arms}
json.dump(out,open(W/'sbr.json','w'),indent=0)
keys=sorted({k for s in out for a in arms for k in out[s][a]})
print('arm   '+' '.join(f'{k:>7s}' for k in keys))
for a in arms:print(f'{a:5s} '+' '.join(f"{st.mean(out[s][a].get(k,0) for s in out):7.3f}" for k in keys))
