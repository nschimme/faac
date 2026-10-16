import sys,os,glob,subprocess
from concurrent.futures import ThreadPoolExecutor
corp,pre,par=sys.argv[1],sys.argv[2],int(sys.argv[3])
rates=[int(x) for x in (sys.argv[4] if len(sys.argv)>4 else '96,128,160,192,256').split(',')]
clips=sorted(glob.glob(corp+'/*.wav'))
jobs=[(c,r) for i,c in enumerate(clips) if i%2==par for r in rates]
def one(j):
    c,r=j; s=os.path.basename(c)[:-4]
    out=f'/tmp/claude/log/{pre}_{s}_{r}.bin'
    subprocess.run(['/tmp/claude/b_search/frontend/faac','-b',str(r),'--overwrite','-o','/tmp/claude/log/tmp.%s.%d.m4a'%(pre,hash(j)%10**9),c],env={**os.environ,'SFLOG':out},capture_output=True,check=True)
with ThreadPoolExecutor(4) as ex: list(ex.map(one,jobs))
for f in glob.glob('/tmp/claude/log/tmp.*'): os.remove(f)
print('done',len(jobs))
