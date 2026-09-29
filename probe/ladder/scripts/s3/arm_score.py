"""Encode step1 arms from <G>/<k>_G_<arm>.bin (+_origin.bin), decode, drop the +64 pad, score serially.
Also scores the anchors (base<lo>, base<hi>, apple) once. Results -> <G>/<out>.json.
usage: arm_score.py <G> <out> <arm,arm,...>   env: LADDER_RATE, LADDER_SLOPE, FAAC_BIN (+ encoder knobs)"""
import os,sys,re,json,subprocess,pathlib,numpy as np,soundfile as sf
G=pathlib.Path(sys.argv[1]);OUT=G/(sys.argv[2]+'.json');arms=sys.argv[3].split(',')
repo=pathlib.Path(__file__).resolve().parents[4]
PAD=int(os.environ.get('LADDER_PAD','64'));rate=os.environ.get('LADDER_RATE','128');lo,hi=os.environ.get('LADDER_SLOPE','112,144').split(',')
faac=os.environ.get('FAAC_BIN',str(repo/'build_ladder/frontend/faac'))
SC=[sys.executable,'/opt/faac-benchmark/scripts/score_clip.py']
res=json.load(open(OUT)) if OUT.exists() else {}
def score(src,deg):
    p=subprocess.run(SC+[str(src),str(deg)],capture_output=True,text=True);m=re.search(r'MOS: ([\d.]+)',p.stdout)
    if not m:raise RuntimeError(p.stdout+p.stderr)
    return float(m.group(1))
def pcm(m):
    return np.frombuffer(subprocess.run(['ffmpeg','-v','error','-i',str(m),'-f','f32le','-ac','2','-'],capture_output=True,check=True).stdout,'<f4').reshape(-1,2)
for it in json.load(open(G/'g2_index.json')):
    k=it['id'];src=it['src'];n=sf.info(src).frames;r=res.setdefault(k,{'stem':it['stem']})
    for a in [f'base{lo}',f'base{hi}','apple']:
        if a in r:continue
        p=it['ref'] if a=='apple' else G/f'{k}_{a}.m4a';r[a]={'mos':score(src,p),'bytes':os.path.getsize(p)}
    for arm in arms:
        if arm in r:continue
        if arm.startswith('H_'):enc=G/f'{k}_{arm}.m4a'   # an existing stream (core-inject arms), scored as is
        else:
            b=G/f'{k}_G_{arm}.bin';o=G/f'{k}_G_{arm}_origin.bin';enc=G/f'{k}_s_{arm}.m4a'
            env=dict(os.environ,FAAC_STEP1=str(b),FAAC_STEP1_OFFSET=os.environ.get('STEP1_OFFSET','1'))
            if o.exists():env['FAAC_STEP1_ORIGIN']=str(o)
            subprocess.run([faac,'--overwrite','-b',rate,'-o',str(enc),str(G/f'{k}_plus.wav')],env=env,capture_output=True,check=True)
        x=pcm(enc);wav=G/f'{k}_tmp.wav';sf.write(wav,x[PAD:PAD+n],48000,subtype='FLOAT')
        r[arm]={'mos':score(src,wav),'bytes':os.path.getsize(enc)};os.remove(wav)
        if not arm.startswith('H_'):os.remove(enc)
        print(k,arm,r[arm],flush=True)
    json.dump(res,open(OUT,'w'),indent=0)
print('DONE')
