import os,sys,glob,json,io,contextlib
from multiprocessing import Pool
sys.path.insert(0,'/Users/nschimme/gitprojects/faac-benchmark'); sys.path.insert(0,'/Users/nschimme/gitprojects/faac-benchmark/scripts')
from score_clip import score_clip
CL={os.path.basename(c)[:-4]:c for c in glob.glob('/Users/nschimme/gitprojects/faac-benchmark/data/external/audio/*.wav')}
def job(a):
    tag,r,n=a; f=f'arm/{tag}/{r}/{n}.aac'
    with contextlib.redirect_stdout(io.StringIO()): m,_=score_clip(CL[n],f)
    return tag,r,n,m,os.path.getsize(f)
if __name__=='__main__':
    jobs=[(t,r,n) for t in ['T0.5','T0.25','T0'] for r in (32,48) for n in CL]
    with Pool(8) as p: res=p.map(job,jobs,chunksize=4)
    json.dump(res,open('mos_ts.json','w')); print(len(res))
