"""exp.py tag rate srcdir -- <faac args>   (env passes through). Encodes 49 clips, scores, writes exp_<tag>.json"""
import os, sys, glob, json, subprocess, multiprocessing as mp, io, contextlib
os.environ['NUMBA_DISABLE_JIT']='1'
sys.path.insert(0,'/opt/faac-benchmark'); sys.path.insert(0,'/opt/faac-benchmark/scripts')
EXT='/opt/faac-benchmark/data/external'
FAAC=os.environ.get('FAAC_BIN','/home/user/work/lc-mid/b/frontend/faac')
def work(a):
    from score_clip import score_clip
    tag,rate,sd,args,n=a
    d=f'/home/user/work/an/d/exp_{tag}'; os.makedirs(d,exist_ok=True)
    out=f'{d}/{n}.m4a'; w=f'{EXT}/{sd}/{n}.wav'
    subprocess.run([FAAC,'-b',str(rate),'--object-type','lc',*args,'-o',out,w],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,check=True)
    with contextlib.redirect_stdout(io.StringIO()):
        r=score_clip(w,out)
    aac=subprocess.run(['ffmpeg','-v','error','-i',out,'-c','copy','-f','adts','-'],capture_output=True).stdout
    return (n,r[0],len(aac))
if __name__=='__main__':
    tag,rate,sd=sys.argv[1:4]; args=sys.argv[5:]
    ns=[os.path.basename(p)[:-4] for p in sorted(glob.glob(f'{EXT}/{sd}/*.wav'))]
    with mp.Pool(4) as p: res=p.map(work,[(tag,rate,sd,args,n) for n in ns],chunksize=1)
    json.dump(res,open(f'/home/user/work/an/exp_{tag}.json','w'))
    import statistics as st
    print(tag,'mean MOS %.4f bytes %d'%(st.mean(r[1] for r in res),sum(r[2] for r in res)))
