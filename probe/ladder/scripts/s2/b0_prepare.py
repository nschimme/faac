import os,json,subprocess,concurrent.futures as cf
G='/home/user/ladder_work_m/g';F='/home/user/faac/build_ladder/frontend/faac';D='/tmp/faad-ladder-dump/build_faad/frontend/faad'
idx=json.load(open(G+'/g2_index.json'))
def one(it):
    k=it['id'];m=f'{G}/{k}_smooth.m4a';d=f'{G}/{k}_smooth.dump'
    if not os.path.exists(d):
        subprocess.run([F,'--overwrite','-b','128','-o',m,f'{G}/{k}_plus.wav'],env=dict(os.environ,FAAC_SF_SMOOTH='0.6'),capture_output=True,check=True)
        subprocess.run([D,'-q','-o',f'{G}/{k}_smooth.wav',m],env=dict(os.environ,FAAD_DUMP=d,FAAD_LADDER_DUMP='1'),capture_output=True,check=True)
    return k
with cf.ThreadPoolExecutor(3) as ex:print(len(list(ex.map(one,idx))))
