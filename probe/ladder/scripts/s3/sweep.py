"""Encode (parallel) + score (serial) arms over the 49-clip set. Usage:
   sweep.py <name> <rate> 'ENV=V,ENV2=V2' [binary]    -> adds scores to results.json"""
import os,sys,subprocess,glob,json,concurrent.futures as cf,re,fcntl
A=os.environ.get('FAAC_BENCHMARK_DATA','/opt/faac-benchmark/data/external/audio');W=os.environ.get('SWEEP_W','/home/user/lw/sw');os.makedirs(W,exist_ok=True)
SC=['/opt/venv312/bin/python3','/opt/faac-benchmark/scripts/score_clip.py']
name,rate,envs=sys.argv[1],sys.argv[2],sys.argv[3];binp=sys.argv[4] if len(sys.argv)>4 else os.environ.get('FAAC_BIN','/home/user/lw/bin/faac_probe2')
env=dict(os.environ,**dict(kv.split('=') for kv in envs.split(',') if kv))
clips=sorted(glob.glob(A+'/*.wav'))
RF=f'{W}/results.json'
res=json.load(open(RF)) if os.path.exists(RF) else {}
def enc(c):
    st=os.path.basename(c)[:-4];o=f'{W}/{name}__{st}.m4a'
    if not os.path.exists(o):subprocess.run([binp,'-b',rate,'-o',o,c],env=env,capture_output=True,check=True)
    return st,o,c
with cf.ThreadPoolExecutor(3) as ex:jobs=list(ex.map(enc,clips))
for st,o,c in jobs:
    if res.get(name,{}).get(st):continue
    out=subprocess.run(SC+[c,o],capture_output=True,text=True,env=dict(os.environ,PATH='/opt/venv312/bin:'+os.environ['PATH'])).stdout
    m=re.search(r'MOS:\s*([0-9.]+)',out);r=json.load(open(RF)) if os.path.exists(RF) else {}
    r.setdefault(name,{})[st]={'mos':float(m.group(1)),'bytes':os.path.getsize(o)};json.dump(r,open(RF,'w'),indent=0);res=r
    os.remove(o)
print(name,'done',len(res[name]))
