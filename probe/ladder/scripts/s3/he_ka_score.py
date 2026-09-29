"""E0.2 functional KA at HE: FAAC's step1 stream at all-Apple core decisions (<k>_G_A.m4a) with Apple's SBR tail
spliced in (A frame i + Apple frame i+1 tail), decoded raw, delay found by cross-correlation (low band), scored
against the source; Apple scored from its m4a. Bits-adjusted with FAAC's base28/base40 slope.
usage: he_ka_score.py <G>"""
import sys,os,json,re,pathlib,subprocess,numpy as np,soundfile as sf,scipy.signal as ss
here=pathlib.Path(__file__).resolve();sys.path.insert(0,str(here.parent))
from he_splice import raw_aus,cpe_bits,splice,adts
G=pathlib.Path(sys.argv[1]);faad='/tmp/faad-ladder-dump/build_faad/frontend/faad';SC=[sys.executable,'/opt/faac-benchmark/scripts/score_clip.py']
lo,hi=os.environ.get('LADDER_SLOPE','28,40').split(',');OUT=G/'he_ka.json';res=json.load(open(OUT)) if OUT.exists() else {}
def bits(stream,tag,k):
    d=G/f'{k}_{tag}_adts.dump';d.unlink(missing_ok=True)
    subprocess.run([faad,'-q','-f','raw','-o',str(G/'t.raw'),str(stream)],env=dict(os.environ,FAAD_DUMP=str(d),FAAD_LADDER_DUMP='1'),capture_output=True,check=True)
    b=cpe_bits(d);d.unlink();return b
def score(src,deg):
    p=subprocess.run(SC+[str(src),str(deg)],capture_output=True,text=True);return float(re.search(r'MOS: ([\d.]+)',p.stdout).group(1))
for it in json.load(open(G/'g2_index.json')):
    k=it['id']
    if k in res:continue
    ap=raw_aus(it['ref'],G/'t.adts');fa=raw_aus(G/f'{k}_G_A.m4a',G/'t.adts')
    apf=G/'ap.adts';open(apf,'wb').write(b''.join(adts(a,24000) for a in ap));fap=G/'fa.adts';open(fap,'wb').write(b''.join(adts(a,24000) for a in fa))
    ab=bits(apf,'ap',k);fb=bits(fap,'fa',k);n=min(len(fa),len(ap)-1)
    sp=[ap[0]]+[splice(fa[i],fb[i+1],ap[i+1],ab[i+2]) for i in range(n)]   # Apple AU 0 carries the first SBR header
    spf=G/'sp.adts';open(spf,'wb').write(b''.join(adts(a,24000) for a in sp))
    subprocess.run([faad,'-q','-o',str(G/'sp.wav'),str(spf)],capture_output=True,check=True)
    y,_=sf.read(G/'sp.wav');s,_=sf.read(it['src']);b,a=ss.butter(6,4000/24000)
    xs=ss.filtfilt(b,a,s.mean(1));ys=ss.filtfilt(b,a,y.mean(1));m=min(len(xs),len(ys))
    c=ss.correlate(ys[:m],xs[:m],mode='full',method='fft')[m-1:m-1+12000];d=int(np.argmax(c))
    w=G/'sp_al.wav';sf.write(w,y[d:d+len(s)],48000,subtype='FLOAT')
    r={'stem':it['stem'],'delay':d,'A':{'mos':score(it['src'],w),'bytes':sum(map(len,sp[1:]))},'apple':{'mos':score(it['src'],it['ref']),'bytes':sum(map(len,ap[1:n+1]))}}
    for q in (lo,hi):p=G/f'{k}_base{q}.m4a';r['base'+q]={'mos':score(it['src'],p),'bytes':os.path.getsize(p)}
    res[k]=r;json.dump(res,open(OUT,'w'),indent=0);print(k,r,flush=True)
    for f in (apf,fap,spf,G/'sp.wav',w,G/'t.raw'):pathlib.Path(f).unlink(missing_ok=True)
print('DONE')
