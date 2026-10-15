import sys,os,json,subprocess
from multiprocessing import Pool
S='/tmp/claude-0/-home-user-faac/bc9c8615-9277-5565-96c9-f1ff39d121b8/scratchpad'
sys.path.insert(0,S+'/fb'); from phase2_mos import score_wav_pair
held=sys.argv[1]=='held'
AUD=S+('/held' if held else '/fb/data/external/audio'); OUT=S+'/run/out'+('_held' if held else '')
def job(a):
    arm,clip,rate=a; enc=f'{OUT}/{arm}/{rate}/{clip}.m4a'; dec=f'/tmp/ff_{os.getpid()}.wav'
    subprocess.run(['ffmpeg','-v','error','-y','-i',enc,'-ar','48000','-sample_fmt','s16',dec],check=True)
    m,_=score_wav_pair(f'{AUD}/{clip}',dec,mode_str='audio',sample_rate=48000); os.remove(dec)
    return dict(arm=arm,clip=clip,rate=rate,mos=m)
if __name__=='__main__':
    arms=sys.argv[2].split(','); clips=sorted(c for c in os.listdir(AUD) if c.endswith('.wav'))
    jobs=[(a,c,r) for a in arms for c in clips for r in (40,48,56,64)]
    with Pool(4) as p: res=p.map(job,jobs)
    json.dump(res,open(f'ff_{sys.argv[1]}.json','w'))
