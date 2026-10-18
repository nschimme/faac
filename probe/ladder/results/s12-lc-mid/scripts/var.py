"""var.py tag gate|all [FAAC_BIN=..] -- encodes+scores a variant (env passes through) over all ladder rungs; writes var_<tag>.json
and prints BD-rate vs Apple."""
import os, sys, glob, json, subprocess, multiprocessing as mp, io, contextlib, statistics as st
os.environ['NUMBA_DISABLE_JIT']='1'
sys.path.insert(0,'/opt/faac-benchmark'); sys.path.insert(0,'/opt/faac-benchmark/scripts'); sys.path.insert(0,'/home/user/work/an')
EXT='/opt/faac-benchmark/data/external'
FAAC=os.environ.get('FAAC_BIN','/home/user/work/probe-bin/faac_bs')
RUNGS={'48k':(96,128,160,192,256,320),'44k1':(128,160,192,256)}
SRC={'48k':'audio','44k1':'audio_44k1'}
GATE=['sandman.16b48k','velvet.16b48k','21-classic.441.16b48k','fms']
def work(a):
    from score_clip import score_clip
    tag,fam,rate,n=a
    d=f'/tmp/varenc/{tag}/{fam}{rate}'; os.makedirs(d,exist_ok=True)
    out=f'{d}/{n}.m4a'; w=f'{EXT}/{SRC[fam]}/{n}.wav'
    subprocess.run([FAAC,'-b',str(rate),'--object-type','lc','--overwrite',*os.environ.get('FAAC_EXTRA','').split(),'-o',out,w],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,check=True)
    with contextlib.redirect_stdout(io.StringIO()): r=score_clip(w,out)
    aac=subprocess.run(['ffmpeg','-v','error','-i',out,'-c','copy','-f','adts','-'],capture_output=True).stdout
    os.remove(out)
    return (fam,rate,n,r[0],len(aac))
if __name__=='__main__':
    tag,which=sys.argv[1:3]
    ns=GATE if which=='gate' else [os.path.basename(p)[:-4] for p in sorted(glob.glob(f'{EXT}/audio/*.wav'))]
    jobs=[(tag,f,r,n) for f in RUNGS for r in RUNGS[f] for n in ns]
    with mp.Pool(4) as p: res=p.map(work,jobs,chunksize=1)
    base=json.load(open('/home/user/work/an/store.json'))
    fa={k:dict(v) for k,v in base['faac'].items()}
    for fam,r,n,m,b in res: fa[f'{fam}|{n}'][str(r)]=(m,b)
    json.dump({'faac':fa,'ns':ns},open(f'/home/user/work/an/var_{tag}.json','w'))
    import bd
    for label,cl in (('GATE',GATE),('ALL49' if which=='all' else None,None)):
        if label: bd.report(f'{tag} {label}',fa,cl,base['apple'])
