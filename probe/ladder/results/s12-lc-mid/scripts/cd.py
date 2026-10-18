"""Coordinate descent on the tuned-profile masking model, objective = mean BD-rate vs Apple (avg of 48k and 44.1k family means).
usage: cd.py FOLD   (A: fit even clips / test odd, B: fit odd / test even, ALL: fit all)
Encodes are cached in cd_cache.json keyed by (params, fam, rung, clip)."""
import os, sys, glob, json, subprocess, multiprocessing as mp, io, contextlib, statistics as st, time
os.environ['NUMBA_DISABLE_JIT']='1'
sys.path.insert(0,'/opt/faac-benchmark'); sys.path.insert(0,'/opt/faac-benchmark/scripts'); sys.path.insert(0,'/home/user/work/an')
EXT='/opt/faac-benchmark/data/external'; FAAC='/home/user/work/probe-bin/faac_cd'
RUNGS={'48k':(96,128,160,192,256),'44k1':(128,160,192,256)}; SRC={'48k':'audio','44k1':'audio_44k1'}
CACHE='/home/user/work/an/cd_cache.json'
START={'pf':0.010,'le':0.30,'ss':3.0,'aw':0.15,'pw':0.45,'af':0.001,'qb':0.0003,'sm':0.6,'rs':3.0,'tlo':0.0,'tmid':0.0,'thi':0.0}
STEP={'pf':('mul',1.4),'le':('add',0.02),'ss':('mul',1.25),'aw':('add',0.03),'pw':('mul',1.3),'af':('mul',2.5),'qb':('mul',2.5),'sm':('add',0.15),'rs':('add',0.5),'tlo':('add',1.0),'tmid':('add',1.0),'thi':('add',1.0)}
ORDER=['le','pf','ss','aw','pw','qb','af','sm','tlo','tmid','thi','rs']
def mmstr(p):
    d=dict(p); out=[f'{k}={d[k]:.6g}' for k in ('pf','le','ss','aw','pw','af','qb','sm','rs')]
    for key,idx in (('tlo',(0,1,2)),('tmid',(3,4)),('thi',(5,6,7))):
        if d[key]: out+= [f't{i}={d[key]:.4g}' for i in idx]
    return ','.join(out)
def work(a):
    from score_clip import score_clip
    mm,fam,rate,n=a
    d=f'/tmp/cdenc/{os.getpid()}'; os.makedirs(d,exist_ok=True); out=f'{d}/{n}.m4a'; w=f'{EXT}/{SRC[fam]}/{n}.wav'
    env=dict(os.environ,FAAC_MM=mm)
    subprocess.run([FAAC,'-b',str(rate),'--object-type','lc','--overwrite','-o',out,w],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,check=True,env=env)
    with contextlib.redirect_stdout(io.StringIO()): r=score_clip(w,out)
    aac=subprocess.run(['ffmpeg','-v','error','-i',out,'-c','copy','-f','adts','-'],capture_output=True).stdout
    os.remove(out); return (mm,fam,rate,n,r[0],len(aac))
APPLE=json.load(open('/home/user/work/an/store.json'))['apple']
import bd
def evaluate(pool,cache,p,clips):
    mm=mmstr(p); jobs=[]
    for fam in RUNGS:
        for r in RUNGS[fam]:
            for n in clips:
                if f'{mm}|{fam}|{r}|{n}' not in cache: jobs.append((mm,fam,r,n))
    for mm_,fam,r,n,m,b in pool.map(work,jobs,chunksize=1): cache[f'{mm_}|{fam}|{r}|{n}']=(m,b)
    if jobs: json.dump(cache,open(CACHE,'w'))
    res={}
    for fam in RUNGS:
        fa={}
        for n in clips: fa[f'{fam}|{n}']={str(r):cache[f'{mm}|{fam}|{r}|{n}'] for r in RUNGS[fam]}
        c=bd.curves({k:v for k,v in APPLE.items() if k in fa},fa,(fam,),None,RUNGS[fam])
        v=[x for x in c.values() if x is not None]; res[fam]=(st.mean(v),max(v))
    return (res['48k'][0]+res['44k1'][0])/2,res
def main():
    fold=sys.argv[1]
    allc=sorted(os.path.basename(p)[:-4] for p in glob.glob(f'{EXT}/audio/*.wav'))
    fit={'A':allc[0::2],'B':allc[1::2],'ALL':allc}[fold]; test={'A':allc[1::2],'B':allc[0::2],'ALL':[]}[fold]
    cache=json.load(open(CACHE)) if os.path.exists(CACHE) else {}
    pool=mp.Pool(4); t0=time.time()
    p=dict(START); best,res=evaluate(pool,cache,p,fit)
    print(f'[{fold}] start fit BD {best:+.3f}  48k {res["48k"][0]:+.2f} 44k1 {res["44k1"][0]:+.2f}  ({time.time()-t0:.0f}s)',flush=True)
    if test:
        tb,tr=evaluate(pool,cache,p,test); print(f'[{fold}] start TEST BD {tb:+.3f}  48k {tr["48k"][0]:+.2f} 44k1 {tr["44k1"][0]:+.2f}',flush=True); test0=tb
    for pas in (1,2):
        scale=1.0 if pas==1 else 0.5
        moved=False
        for k in ORDER:
            kind,s=STEP[k]
            def mk(sign,mult=1):
                q=dict(p)
                if kind=='mul': q[k]=p[k]*(s**(sign*scale))**mult
                else: q[k]=p[k]+sign*s*scale*mult
                return q
            for sign in (+1,-1):
                improved=False
                while True:
                    q=mk(sign); v,r=evaluate(pool,cache,q,fit)
                    ok=v<best-0.15
                    print(f'[{fold}] p{pas} {k} {p[k]:.5g}->{q[k]:.5g}: {v:+.3f} (best {best:+.3f}) {"ACCEPT" if ok else "reject"} ({time.time()-t0:.0f}s)',flush=True)
                    if not ok: break
                    p,best,res=q,v,r; improved=moved=True
                if improved: break
        if test:
            tb,tr=evaluate(pool,cache,p,test)
            print(f'[{fold}] after pass {pas}: fit {best:+.3f} TEST {tb:+.3f} (start TEST {test0:+.3f}) params {json.dumps({k:round(v,5) for k,v in p.items()})}',flush=True)
        else:
            print(f'[{fold}] after pass {pas}: fit {best:+.3f} params {json.dumps({k:round(v,5) for k,v in p.items()})}',flush=True)
        if not moved: break
    print(f'[{fold}] FOLDDONE',flush=True)
main()
