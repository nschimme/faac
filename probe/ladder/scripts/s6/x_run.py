"""S6 core/SBR split, one reference at one rate.
usage: x_run.py <W> <rate> <ref: fdk|apple_he48k|apple_he32k> <start> <pad> <k> <lo,hi> <ctl|score>
  W      work dir (own results files; m4a cache in W/enc)
  start  FAAC_SBR_START giving the reference's kx (Fm arm); FAAC base F keeps the default
  pad    zeros prepended to FAAC's input (48 kHz samples), k: FAAC AU i pairs with reference AU i+k
ctl:   encode, alignment (decoded lag of Fm + 2048k == lag of X, +/-1), Control S (self-splice byte-identical) -> ctl.json
score: arms F, Fm, X, FmC+XS, XC+FmS, FC+XS, anchors s<lo>/s<hi>, decoded through FAAD, serial score -> res.json
FAAC env comes from the caller (base knobs)."""
import os,sys,re,json,wave,pathlib,subprocess,concurrent.futures as cf
here=pathlib.Path(__file__).resolve();sys.path.insert(0,str(here.parent))
from xs import raw_aus,core_bits,decode,lag,mix,splice
W=pathlib.Path(sys.argv[1]);rate=sys.argv[2];ref=sys.argv[3];start=sys.argv[4];P=int(sys.argv[5]);k=int(sys.argv[6])
lo,hi=sys.argv[7].split(',');mode=sys.argv[8]
A='/opt/faac-benchmark/data/external/audio';FAAC=os.environ.get('FAAC_BIN','/home/user/lw/bin/faac_probe')
SC=['/opt/venv312/bin/python3','/opt/faac-benchmark/scripts/score_clip.py'];REF=here.parents[2]/'ref'
E=W/'enc';E.mkdir(parents=True,exist_ok=True)
stems=sorted(p.stem for p in pathlib.Path(A).glob('*.wav'))
if os.environ.get('X_CLIPS'):stems=[s for s in stems if any(s.startswith(c) for c in os.environ['X_CLIPS'].split(','))]
def padded(st):
    o=W/f'{st}_p.wav'
    if not o.exists():
        with wave.open(f'{A}/{st}.wav','rb') as w:pr=w.getparams();pcm=w.readframes(w.getnframes())
        with wave.open(str(o),'wb') as w:w.setparams(pr);w.writeframes(b'\0'*P*pr.nchannels*pr.sampwidth+pcm)
    return o
def enc(job):
    st,arm=job;o=E/f'{arm}__{st}.m4a'
    if o.exists():return
    if arm=='X':
        if ref=='fdk':subprocess.run(['/home/user/lw/bin/fdk_he','-b',rate,'-o',str(o),f'{A}/{st}.wav'],check=True,capture_output=True)
        return
    r={'F':rate,'Fm':rate,'s'+lo:lo,'s'+hi:hi}[arm];env=dict(os.environ)
    if arm=='Fm':env['FAAC_SBR_START']=start
    subprocess.run([FAAC,'-b',r,'-o',str(o),str(padded(st))],env=env,check=True,capture_output=True)
def xpath(st):return E/f'X__{st}.m4a' if ref=='fdk' else REF/ref/f'{st}.m4a'
for s in stems:padded(s)   # serially: three encoders share one padded input
jobs=[(s,a) for s in stems for a in ('X','F','Fm','s'+lo,'s'+hi)]
with cf.ThreadPoolExecutor(3) as ex:list(ex.map(enc,jobs))
for s in stems:(W/f'{s}_p.wav').unlink(missing_ok=True)
def load(st):
    au={a:raw_aus(E/f'{a}__{st}.m4a' if a!='X' else xpath(st),W/'t.adts') for a in ('F','Fm','X')}
    cb={a:core_bits(au[a],W/f't_{a}.adts') for a in au};return au,cb
out=W/('ctl.json' if mode=='ctl' else 'res.json');res=json.load(open(out)) if out.exists() else {}
for st in stems:
    if mode=='ctl':
        if st in res:continue
        au,cb=load(st);r={}
        for a in au:r['selfsplice_bad_'+a]=sum(splice(au[a][i],cb[a][i],au[a][i],cb[a][i])!=au[a][i] for i in cb[a] if i<len(au[a]))
        for a in ('Fm','X'):
            w=W/f'l_{a}.wav';decode(au[a],w);r['lag_'+a]=lag(f'{A}/{st}.wav',w);w.unlink()
        r['lag_ok']=abs(r['lag_Fm']+2048*k-r['lag_X'])<=1;r['frames']={a:len(au[a]) for a in au}
        res[st]=r;json.dump(res,open(out,'w'),indent=0);print(st,r,flush=True);continue
    au,cb=load(st)
    arms={'F':au['F'],'Fm':au['Fm'],'X':au['X'],
          'FmC+XS':mix(au['Fm'],cb['Fm'],au['X'],cb['X'],k),
          'XC+FmS':mix(au['X'],cb['X'],au['Fm'],cb['Fm'],-k),
          'FC+XS':mix(au['F'],cb['F'],au['X'],cb['X'],k)}
    for a in ('s'+lo,'s'+hi):arms[a]=raw_aus(E/f'{a}__{st}.m4a',W/'t.adts')
    for a,aus in arms.items():
        if st in res.get(a,{}):continue
        w=W/f'd_{a}.wav';decode(aus,w)
        o=subprocess.run(SC+[f'{A}/{st}.wav',str(w)],capture_output=True,text=True).stdout;w.unlink()
        res.setdefault(a,{})[st]={'mos':float(re.search(r'MOS:\s*([0-9.]+)',o).group(1)),'bytes':sum(map(len,aus))}
        json.dump(res,open(out,'w'),indent=0)
    print(st,{a:res[a][st]['mos'] for a in arms},flush=True)
if mode=='ctl':
    print('lag_ok',sum(r['lag_ok'] for r in res.values()),'/',len(res),'selfsplice all zero',
          sum(all(v==0 for kk,v in r.items() if kk.startswith('selfsplice')) for r in res.values()),'/',len(res))
print('DONE',mode)
