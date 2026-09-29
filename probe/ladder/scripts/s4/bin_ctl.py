"""Binary control: decoded PCM of binary B equals binary A, all 49 clips. usage: bin_ctl.py <A> <B> <rate> [faac args]  (env passed through)"""
import os,sys,glob,hashlib,subprocess,tempfile,concurrent.futures as cf
D=os.environ.get('FAAC_BENCHMARK_DATA','/opt/faac-benchmark/data/external/audio');a,b,rate=sys.argv[1:4];extra=sys.argv[4:];T=tempfile.mkdtemp(dir='/home/user/lw')
def h(binp,c,t):
    o=f'{T}/{t}_{os.path.basename(c)}.m4a';subprocess.run([binp,'-b',rate,*extra,'-o',o,c],capture_output=True,check=True)
    x=hashlib.md5(subprocess.run(['ffmpeg','-v','error','-i',o,'-f','s16le','-'],capture_output=True,check=True).stdout).hexdigest();os.remove(o);return x
def one(c):return os.path.basename(c),h(a,c,'a')==h(b,c,'b')
with cf.ThreadPoolExecutor(4) as ex:r=list(ex.map(one,sorted(glob.glob(D+'/*.wav'))))
os.rmdir(T);bad=[s for s,ok in r if not ok];print(f'{rate}k {extra}: {len(r)-len(bad)}/{len(r)} PCM-identical',bad)
