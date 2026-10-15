import sys,os,json,subprocess,pathlib,hashlib,wave
from multiprocessing import Pool
S='/tmp/claude-0/-home-user-faac/bc9c8615-9277-5565-96c9-f1ff39d121b8/scratchpad'
sys.path.insert(0,S+'/fb')
from phase2_mos import score_wav_pair
FAAC=S+'/base/build/frontend/faac'; FAAD=S+'/fb/bin/faad'
FDK=S+'/fdkprefix/bin/fdkaac'
AUD=os.environ.get('AUDDIR',S+'/fb/data/external/audio')
APPLE='/home/user/faac/probe/ladder/ref/apple-s11-367cf8dc/'+('lc' if os.environ.get('PROFILE')=='lc' else 'he')
OUT=S+'/run/out'+('_'+os.environ['TAG'] if os.environ.get('TAG') else ('_held' if os.environ.get('AUDDIR') else ''))+('_lc' if os.environ.get('PROFILE')=='lc' else ''); os.makedirs(OUT,exist_ok=True)
LC=os.environ.get('PROFILE')=='lc'
RUNGS=[int(x) for x in os.environ['RUNGSTR'].split(',')] if os.environ.get('RUNGSTR') else ([96,128,160,192] if LC else [40,48,56,64])
def pkt_bits(f):
    r=subprocess.run(['ffprobe','-v','error','-select_streams','a:0','-show_entries','packet=size','-of','csv=p=0',f],capture_output=True,text=True,check=True)
    return 8*sum(int(x) for x in r.stdout.replace(',',' ').split())
def job(a):
    arm,clip,rate=a
    ref=f'{AUD}/{clip}'; w=wave.open(ref); dur=w.getnframes()/w.getframerate()
    d=f'{OUT}/{arm}/{rate}'; os.makedirs(d,exist_ok=True)
    if arm.startswith(('fix','nofix')):
        enc=f'{d}/{clip}.m4a'
        kind='nofix' if arm.startswith('nofix') else 'exp'; st=arm.lstrip('nofix') if False else arm.replace('nofix','').replace('fix','')
        env=dict(os.environ,FAAC_PROBE_SBR_START=st)
        subprocess.run([f'{S}/{kind}/build/frontend/faac','--object-type','he-aac-v1','--overwrite','-b',str(rate),'-o',enc,ref],check=True,capture_output=True,env=env)
    elif arm=='faac':
        enc=f'{d}/{clip}.m4a'
        subprocess.run([FAAC,'--object-type',('lc' if LC else 'he-aac-v1'),'--overwrite','-b',str(rate),'-o',enc,ref],check=True,capture_output=True)
    elif arm=='fdk':
        enc=f'{d}/{clip}.m4a'
        subprocess.run([FDK,'-p',('2' if LC else '5'),'-b',str(rate*1000),'-o',enc,ref],check=True,capture_output=True)
    else:
        enc=f'{APPLE}/{rate}k/{clip}.m4a'
    dec=f'{d}/{clip}.dec.wav'; dec2=f'{d}/{clip}.dec2.wav'
    r=subprocess.run([FAAD,'-q','-o',dec,enc],capture_output=True)
    if r.returncode or not os.path.exists(dec): return dict(arm=arm,clip=clip,rate=rate,err='decode')
    mos,be=score_wav_pair(ref,dec,mode_str='audio',sample_rate=48000)
    subprocess.run(['ffmpeg','-v','error','-y','-i',enc,'-sample_fmt','s16',dec2],check=True)
    mos2,_=score_wav_pair(ref,dec2,mode_str='audio',sample_rate=48000)
    os.remove(dec); os.remove(dec2)
    return dict(arm=arm,clip=clip,rate=rate,mos=mos,mos_ff=mos2,kbps=pkt_bits(enc)/dur/1000,sha=hashlib.sha256(open(enc,'rb').read()).hexdigest())
if __name__=='__main__':
    clips=sorted(os.listdir(AUD)); clips=[c for c in clips if c.endswith('.wav') and (os.environ.get('AUDDIR') or os.path.exists(f'{APPLE}/{RUNGS[0]}k/{c}.m4a'))]
    arms=sys.argv[1].split(',')
    jobs=[(a,c,r) for a in arms for c in clips for r in RUNGS]
    print(len(clips),'clips',len(jobs),'jobs',flush=True)
    res=[]
    with Pool(4) as p:
        for i,x in enumerate(p.imap_unordered(job,jobs)):
            res.append(x)
            if i%40==0: print(i,flush=True)
    json.dump(res,open(f'{S}/run/rows_{(os.environ['TAG']+'_') if os.environ.get('TAG') else ('held_' if os.environ.get('AUDDIR') else '')}{'lc_' if LC else ''}{sys.argv[1].replace(',','_')}.json','w'))
