import os,sys,subprocess,glob,json
from multiprocessing import Pool
sys.path.insert(0,'/Users/nschimme/gitprojects/faac-benchmark'); sys.path.insert(0,'/Users/nschimme/gitprojects/faac-benchmark/scripts')
from score_clip import score_clip
S=os.getcwd(); FAAC=f'{S}/wt/b/frontend/faac'
CL=sorted(glob.glob('/Users/nschimme/gitprojects/faac-benchmark/data/external/audio/*.wav'))
def job(a):
    arm,rate,clip=a; d=f'{S}/mos/{arm}_{rate}'; os.makedirs(d,exist_ok=True)
    out=f'{d}/{os.path.basename(clip)[:-4]}.aac'
    if os.path.exists(out): os.remove(out)
    env=dict(os.environ); env.pop('FAAC_PNS_VIT',None)
    if arm.startswith('v'): env['FAAC_PNS_VIT']=arm[1:]
    subprocess.run([FAAC,'-a','-b',str(rate),'-o',out,clip],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    import io,contextlib
    with contextlib.redirect_stdout(io.StringIO()):
        mos,_=score_clip(clip,out)
    return arm,rate,os.path.basename(clip),mos,os.path.getsize(out)
if __name__=='__main__':
    jobs=[]
    for r in (16,24,32,48):
        for arm in ('base','v1','v2'): jobs+=[(arm,r,c) for c in CL]
    for r in (12,20,28,40,56): jobs+=[('base',r,c) for c in CL]
    with Pool(8) as p: res=p.map(job,jobs,chunksize=4)
    json.dump(res,open('mos_res.json','w'))
    print(len(res),'done')
