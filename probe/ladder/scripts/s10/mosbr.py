import os,sys,glob,json,io,contextlib,subprocess
from multiprocessing import Pool
sys.path.insert(0,'/Users/nschimme/gitprojects/faac-benchmark'); sys.path.insert(0,'/Users/nschimme/gitprojects/faac-benchmark/scripts')
from score_clip import score_clip
S=os.getcwd(); CL={os.path.basename(c)[:-4]:c for c in glob.glob('/Users/nschimme/gitprojects/faac-benchmark/data/external/audio/*.wav')}
def job(a):
    pl,r,n=a; d=f'{S}/br/p{pl}_{r}'; os.makedirs(d,exist_ok=True); out=f'{d}/{n}.aac'
    if os.path.exists(out): os.remove(out)
    env=dict(os.environ); env.pop('FAAC_SBR_BSF',None); env.pop('FAAC_PNS_VIT',None)
    if pl!=4: env['FAAC_PNSL']=str(pl)
    subprocess.run([f'{S}/wt/b/frontend/faac','-a','-b',str(r),'-o',out,CL[n]],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    with contextlib.redirect_stdout(io.StringIO()): m,_=score_clip(CL[n],out)
    return pl,r,n,m,os.path.getsize(out)
if __name__=='__main__':
    jobs=[]
    for r in (16,24,40,64): jobs+=[(pl,r,n) for pl in (4,3,5) for n in CL]
    for r in (32,48): jobs+=[(5,r,n) for n in CL]
    with Pool(8) as p: res=p.map(job,jobs,chunksize=4)
    json.dump(res,open('mos_br.json','w')); print(len(res))
