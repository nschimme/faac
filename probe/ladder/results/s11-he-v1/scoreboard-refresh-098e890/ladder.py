import sys,os,json,subprocess,pathlib,hashlib,wave
from multiprocessing import Pool
S='/tmp/claude-0/-home-user-faac/bc9c8615-9277-5565-96c9-f1ff39d121b8/scratchpad'
sys.path.insert(0,S+'/fb')
from phase2_mos import score_wav_pair
FAAC=S+'/base/build/frontend/faac'; FAAD=S+'/fb/bin/faad'
AUD=S+'/fb/data/external/audio'
APPLE='/home/user/faac/probe/ladder/ref/apple-s11-367cf8dc/he'
OUT=S+'/run/out'; os.makedirs(OUT,exist_ok=True)
RUNGS=[40,48,56,64]
def pkt_bits(f):
    r=subprocess.run(['ffprobe','-v','error','-select_streams','a:0','-show_entries','packet=size','-of','csv=p=0',f],capture_output=True,text=True,check=True)
    return 8*sum(int(x) for x in r.stdout.replace(',',' ').split())
def job(a):
    arm,clip,rate=a
    ref=f'{AUD}/{clip}'; w=wave.open(ref); dur=w.getnframes()/w.getframerate()
    d=f'{OUT}/{arm}/{rate}'; os.makedirs(d,exist_ok=True)
    if arm=='faac':
        enc=f'{d}/{clip}.m4a'
        subprocess.run([FAAC,'--object-type','he-aac-v1','--overwrite','-b',str(rate),'-o',enc,ref],check=True,capture_output=True)
    else:
        enc=f'{APPLE}/{rate}k/{clip}.m4a'
    dec=f'{d}/{clip}.dec.wav'
    r=subprocess.run([FAAD,'-q','-o',dec,enc],capture_output=True)
    if r.returncode or not os.path.exists(dec): return dict(arm=arm,clip=clip,rate=rate,err='decode')
    mos,be=score_wav_pair(ref,dec,mode_str='audio',sample_rate=48000)
    os.remove(dec)
    return dict(arm=arm,clip=clip,rate=rate,mos=mos,kbps=pkt_bits(enc)/dur/1000,sha=hashlib.sha256(open(enc,'rb').read()).hexdigest())
if __name__=='__main__':
    clips=sorted(os.listdir(AUD)); clips=[c for c in clips if c.endswith('.wav') and os.path.exists(f'{APPLE}/40k/{c}.m4a')]
    arms=sys.argv[1].split(',')
    jobs=[(a,c,r) for a in arms for c in clips for r in RUNGS]
    print(len(clips),'clips',len(jobs),'jobs',flush=True)
    res=[]
    with Pool(4) as p:
        for i,x in enumerate(p.imap_unordered(job,jobs)):
            res.append(x)
            if i%40==0: print(i,flush=True)
    json.dump(res,open(f'{S}/run/rows_{sys.argv[1]}.json','w'))
