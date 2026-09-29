"""Neutral-knob control: decoded PCM of <env> encodes equals the base encode, all 49 clips.
usage: knob_ctl.py <rate> 'ENV=V,...' [extra faac args]   (base env FAAC_SF_SMOOTH=0.6 FAAC_BS_DROPRATIO=12)"""
import os,sys,glob,hashlib,subprocess,tempfile,concurrent.futures as cf
A=os.environ.get('FAAC_BENCHMARK_DATA','/opt/faac-benchmark/data/external/audio')
B=os.environ.get('FAAC_BIN','/home/user/lw/bin/faac_probe')
base=dict(os.environ,FAAC_SF_SMOOTH='0.6',FAAC_BS_DROPRATIO='12')
rate,kv=sys.argv[1],sys.argv[2];extra=sys.argv[3:]
arm=dict(base,**dict(x.split('=') for x in kv.split(',') if x))
T=tempfile.mkdtemp(dir=os.environ.get('SWEEP_W','/home/user/lw'))
def pcm(env,c,tag):
    o=f'{T}/{tag}_{os.path.basename(c)}.m4a'
    subprocess.run([B,'-b',rate,*extra,'-o',o,c],env=env,capture_output=True,check=True)
    h=hashlib.md5(subprocess.run(['ffmpeg','-v','error','-i',o,'-f','s16le','-'],capture_output=True,check=True).stdout).hexdigest()
    os.remove(o);return h
def one(c):return os.path.basename(c),pcm(base,c,'b')==pcm(arm,c,'a')
with cf.ThreadPoolExecutor(4) as ex:r=list(ex.map(one,sorted(glob.glob(A+'/*.wav'))))
os.rmdir(T);bad=[s for s,ok in r if not ok]
print(f'{rate}k {kv}: {len(r)-len(bad)}/{len(r)} PCM-identical',bad)
