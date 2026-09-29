# C1: per-frame block-switch detector state (FAAC, +64 input) + Apple's window per frame.
import sys,os,json,subprocess,concurrent.futures as cf
sys.path.insert(0,'/home/user/faac/probe/ladder');from parse_dump import parse
G='/home/user/ladder_work/g';O='/home/user/wsC/c1';P='/home/user/wt/probe-bs/build/frontend/faac'
idx=json.load(open(G+'/g2_index.json'))
def one(it):
    k=it['id'];bs=f'{O}/{k}.bs'
    if not os.path.exists(bs):
        subprocess.run([P,'-b','128','-o',f'{O}/{k}.m4a','--overwrite',f'{G}/{k}_plus.wav'],env=dict(os.environ,FAAC_BS_DUMP=bs),capture_output=True,check=True);os.remove(f'{O}/{k}.m4a')
    d=parse(f'{G}/{k}_apple.dump');aw={f:d[f][0].win_seq for f in d if 0 in d[f] and d[f][0].present}
    json.dump({'stem':it['stem'],'apple':aw},open(f'{O}/{k}.apple.json','w'))
    return k
with cf.ProcessPoolExecutor(3) as ex:
    for k in ex.map(one,idx):print(k,end=' ',flush=True)
