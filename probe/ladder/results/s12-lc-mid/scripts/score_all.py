import os, sys, glob, json, multiprocessing as mp
os.environ['NUMBA_DISABLE_JIT']='1'
sys.path.insert(0,'/opt/faac-benchmark'); sys.path.insert(0,'/opt/faac-benchmark/scripts')
import io, contextlib
SRC='/opt/faac-benchmark/data/external/audio'
def work(a):
    from score_clip import score_clip
    rate,n,e=a
    deg=f'/home/user/work/an/d/{rate}/{n}.{e}.m4a' if e=='faac' else f'/home/user/faac/probe/ladder/ref/apple-s11-367cf8dc/lc/{rate}k/{n}.wav.m4a'
    with contextlib.redirect_stdout(io.StringIO()):
        r=score_clip(f'{SRC}/{n}.wav',deg)
    return (rate,n,e,r[0] if r else None)
if __name__=='__main__':
    rates=sys.argv[1:]
    jobs=[(r,os.path.basename(p)[:-4],e) for r in rates for p in sorted(glob.glob(SRC+'/*.wav')) for e in ('faac','apple')]
    out=f'/home/user/work/an/mos_{"_".join(rates)}.json'
    with mp.Pool(4) as p: res=p.map(work,jobs,chunksize=1)
    json.dump(res,open(out,'w'))
    print('done',len(res))
