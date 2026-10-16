import os,sys,json,subprocess,glob,shutil
from concurrent.futures import ThreadPoolExecutor
sys.path.insert(0,'/tmp/claude/fb')
from phase2_mos import score_wav_pair
# usage: score.py TAG FAAC CORPDIR [ENV=VAL ...]; RUNGS env, default 128,160,192
def one(a):
    faac,env,clip,rate,tag=a
    out=f'/tmp/claude/h/out/{tag}'; os.makedirs(out,exist_ok=True)
    stem=os.path.basename(clip)[:-4]
    m4a=f'{out}/{stem}.{rate}.m4a'; dec=f'{out}/{stem}.{rate}.wav'
    if os.path.exists(m4a): os.remove(m4a)
    e={**os.environ,**env}
    subprocess.run([faac,'-b',str(rate),*os.environ.get('HARNESS_ARGS','').split(),'--overwrite','-o',m4a,clip],env=e,check=True,capture_output=True)
    subprocess.run(['ffmpeg','-nostdin','-v','error','-y','-i',m4a,'-ar','48000','-ac','2',dec],check=True)
    ref=f'/tmp/claude/h/ref48/{os.path.basename(os.path.dirname(clip))}_{stem}.wav'
    if not os.path.exists(ref):
        os.makedirs(os.path.dirname(ref),exist_ok=True)
        subprocess.run(['ffmpeg','-nostdin','-v','error','-y','-i',clip,'-ar','48000','-ac','2',ref],check=True)
    mos,_=score_wav_pair(ref,dec,mode_str='audio',sample_rate=48000)
    dur=float(subprocess.check_output(['ffprobe','-v','error','-show_entries','format=duration','-of','csv=p=0',clip])); kbps=os.path.getsize(m4a)*8/dur/1000
    os.remove(dec); os.remove(m4a)
    return stem,rate,kbps,mos
if __name__=='__main__':
    tag,faac,corp=sys.argv[1:4]
    env=dict(a.split('=',1) for a in sys.argv[4:])
    rates=[int(x) for x in os.environ.get('RUNGS','128,160,192').split(',')]
    import re; clips=[c for c in sorted(glob.glob(corp+'/*.wav')) if re.search(os.environ.get('CLIPS','.'),c)]
    jobs=[(faac,env,c,r,tag) for c in clips for r in rates]
    with ThreadPoolExecutor(int(os.environ.get('JOBS','4'))) as ex: res=list(ex.map(one,jobs))
    json.dump(res,open(f'/tmp/claude/h/{tag}.json','w'))
