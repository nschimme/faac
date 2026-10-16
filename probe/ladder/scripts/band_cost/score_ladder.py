import os,sys,json,subprocess,tempfile,glob
from concurrent.futures import ThreadPoolExecutor
sys.path.insert(0,'/tmp/claude/fb')
from phase2_mos import score_wav_pair
CORP=os.environ.get('CORP','/tmp/claude/fb/data/external/audio')
def one(args):
    faac,env,clip,rate,tag=args
    out=f'/tmp/claude/h/out/{tag}'; os.makedirs(out,exist_ok=True)
    stem=os.path.basename(clip)[:-4]
    m4a=f'{out}/{stem}.{rate}.m4a'; dec=f'{out}/{stem}.{rate}.wav'
    e={**os.environ,**env}
    subprocess.run([faac,'-b',str(rate),*os.environ.get('HARNESS_ARGS','').split(),'-o',m4a,clip],env=e,check=True,capture_output=True)
    subprocess.run(['ffmpeg','-v','error','-y','-i',m4a,'-ar','48000','-ac','2',dec],check=True)
    ref48=f'{out}/{stem}.ref48.wav'
    if not os.path.exists(ref48):
        subprocess.run(['ffmpeg','-v','error','-y','-i',clip,'-ar','48000','-ac','2',ref48],check=True)
    mos,_=score_wav_pair(ref48,dec,mode_str='audio',sample_rate=48000)
    dur=10.0
    kbps=os.path.getsize(m4a)*8/dur/1000
    os.remove(dec)
    return stem,rate,kbps,mos
def run(tag,faac,env,rates=tuple(int(x) for x in os.environ.get('RUNGS','128,160,192').split(','))):
    clips=sorted(glob.glob(CORP+'/*.wav'))
    jobs=[(faac,env,c,r,tag) for c in clips for r in rates]
    with ThreadPoolExecutor(4) as ex: res=list(ex.map(one,jobs))
    json.dump(res,open(f'/tmp/claude/h/{tag}.json','w'))
    return res
if __name__=='__main__':
    tag,faac=sys.argv[1],sys.argv[2]
    env=dict(a.split('=',1) for a in sys.argv[3:])
    run(tag,faac,env)
