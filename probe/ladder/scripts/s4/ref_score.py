"""Score a reference set (m4a per clip) serially into a sweep results.json as arm <name>.
usage: ref_score.py <refdir> <name>   env SWEEP_W"""
import os,sys,re,glob,json,subprocess
A=os.environ.get('FAAC_BENCHMARK_DATA','/opt/faac-benchmark/data/external/audio');RF=os.environ['SWEEP_W']+'/results.json'
SC=['/opt/venv312/bin/python3','/opt/faac-benchmark/scripts/score_clip.py'];d,name=sys.argv[1],sys.argv[2]
for m in sorted(glob.glob(d+'/*.m4a')):
    st=os.path.basename(m)[:-4];r=json.load(open(RF))
    if r.get(name,{}).get(st):continue
    out=subprocess.run(SC+[f'{A}/{st}.wav',m],capture_output=True,text=True).stdout
    r.setdefault(name,{})[st]={'mos':float(re.search(r'MOS:\s*([0-9.]+)',out).group(1)),'bytes':os.path.getsize(m)};json.dump(r,open(RF,'w'),indent=0)
print(name,'done')
