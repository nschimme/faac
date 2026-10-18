import os, sys, glob, json, multiprocessing as mp, io, contextlib
os.environ['NUMBA_DISABLE_JIT']='1'
sys.path.insert(0,'/opt/faac-benchmark'); sys.path.insert(0,'/opt/faac-benchmark/scripts')
EXT='/opt/faac-benchmark/data/external'
REF='/home/user/faac/probe/ladder/ref/apple-s11-extra/lc'
TAGS={'44k1_128k':'audio_44k1','44k1_160k':'audio_44k1','44k1_192k':'audio_44k1','44k1_256k':'audio_44k1','48k_256k':'audio','48k_320k':'audio'}
def work(a):
    from score_clip import score_clip
    tag,n,e=a
    deg=f'/home/user/work/an/d/{tag}/{n}.faac.m4a' if e=='faac' else f'{REF}/{tag}/{n}.wav.m4a'
    if e=='apple' and not os.path.exists(deg): deg=f'{REF}/{tag}/{n}.m4a'
    with contextlib.redirect_stdout(io.StringIO()):
        r=score_clip(f'{EXT}/{TAGS[tag]}/{n}.wav',deg)
    return (tag,n,e,r[0] if r else None)
if __name__=='__main__':
    jobs=[(t,os.path.basename(p)[:-4],e) for t in TAGS for p in sorted(glob.glob(f'{EXT}/{TAGS[t]}/*.wav')) for e in ('faac','apple')]
    with mp.Pool(4) as p: res=p.map(work,jobs,chunksize=1)
    json.dump(res,open('/home/user/work/an/mos_extra.json','w')); print('done',len(res))
