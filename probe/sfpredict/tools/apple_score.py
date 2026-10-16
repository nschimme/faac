import os,sys,json,subprocess,glob,re
from concurrent.futures import ThreadPoolExecutor
sys.path.insert(0,'/tmp/claude/fb')
from phase2_mos import score_wav_pair
A='/home/user/apple/probe/ladder/ref/apple-s11-367cf8dc/lc'
C='/home/user/corpus/c48'
def one(a):
    rate,m4a=a
    stem=os.path.basename(m4a)[:-8]  # strip .wav.m4a
    ref=[c for c in glob.glob(C+'/*.wav') if os.path.basename(c)[:-4].replace('_',' ')==stem.replace('_',' ').replace(',','')][:1]
    return rate,stem,m4a,ref
jobs=[]
for rate in (96,128,160,192):
    for m in sorted(glob.glob(f'{A}/{rate}k/*.m4a')): jobs.append((rate,m))
def norm(s): return re.sub(r'[^A-Za-z0-9]','',s)
refs={norm(os.path.basename(c)[:-4]):c for c in glob.glob(C+'/*.wav')}
def run(a):
    rate,m4a=a
    stem=os.path.basename(m4a)[:-4]
    if stem.endswith('.wav'): stem=stem[:-4]
    key=norm(stem); ref=refs.get(key)
    if ref is None: return None
    dec=f'/tmp/claude/h/ap_{rate}_{key}.wav'
    subprocess.run(['ffmpeg','-nostdin','-v','error','-y','-i',m4a,'-ar','48000','-ac','2',dec],check=True)
    r48=f'/tmp/claude/h/ref48/c48_{os.path.basename(ref)}'
    if not os.path.exists(r48):
        os.makedirs(os.path.dirname(r48),exist_ok=True)
        subprocess.run(['ffmpeg','-nostdin','-v','error','-y','-i',ref,'-ar','48000','-ac','2',r48],check=True)
    mos,_=score_wav_pair(r48,dec,mode_str='audio',sample_rate=48000)
    os.remove(dec)
    dur=float(subprocess.check_output(['ffprobe','-v','error','-show_entries','format=duration','-of','csv=p=0',ref]))
    return os.path.basename(ref)[:-4],rate,os.path.getsize(m4a)*8/dur/1000,mos
with ThreadPoolExecutor(4) as ex: res=[r for r in ex.map(run,jobs) if r]
json.dump(res,open('/tmp/claude/h/apple48.json','w')); print(len(res))
