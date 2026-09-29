import sys
import os,pathlib
_W=pathlib.Path(os.environ.get('LADDER_WORK','./ladder_work'));_REPO=pathlib.Path(__file__).resolve().parents[4]
LADDER_G=pathlib.Path(os.environ.get('LADDER_G',str(_W/'g')));LADDER_H=pathlib.Path(os.environ.get('LADDER_H',str(_W/'h')));LADDER_S=pathlib.Path(os.environ.get('LADDER_S',str(_W/'s')))
FAAC_BIN=os.environ.get('FAAC_BIN',str(_REPO/'build_ladder/frontend/faac'));FAAD_BIN=os.environ.get('FAAD_BIN','/tmp/faad-ladder-dump/build_faad/frontend/faad');SCORE_CLIP=os.environ.get('SCORE_CLIP','/opt/faac-benchmark/scripts/score_clip.py');PYTHON_BIN=os.environ.get('PYTHON_BIN',sys.executable)
FAAC_SMOOTH_BIN=os.environ.get('FAAC_SMOOTH_BIN','/home/user/wt/sf-smooth/build/frontend/faac');FAAC_MASTER_BIN=os.environ.get('FAAC_MASTER_BIN','/home/user/wt/master/build/frontend/faac')
import os,subprocess,pathlib,json,re,numpy as np,soundfile as sf,sys
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'h'));from h_make import make
root=LADDER_H;g=LADDER_G
faac=FAAC_BIN
faad=FAAD_BIN
sc=SCORE_CLIP;py=PYTHON_BIN
index=json.loads((g/'g2_index.json').read_text());out=root/'h2_scores.json';res=json.loads(out.read_text()) if out.exists() else {}
def score(src,wav):
    p=subprocess.run([py,sc,str(src),str(wav)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True);m=re.search(r'MOS: ([\d.]+)',p.stdout)
    if not m:raise RuntimeError(p.stdout)
    return float(m.group(1))
def pcm(m4a):
    raw=root/'tmp.f32';subprocess.run(['ffmpeg','-v','error','-y','-i',str(m4a),'-f','f32le','-ac','2',str(raw)],check=True);x=np.fromfile(raw,dtype='<f4').reshape(-1,2);raw.unlink();return x
for item in index:
    k=item['id']
    if k in res:continue
    src=pathlib.Path(item['src']);n=sf.info(src).frames;r={'stem':item['stem']}
    d=root/f'{k}_W.dump';d.unlink(missing_ok=True)
    subprocess.run([faad,'-q','-o',str(root/'tmp.wav'),str(root/f'{k}_W.m4a')],env=dict(os.environ,FAAD_DUMP=str(d),FAAD_LADDER_DUMP='1'),stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,check=True)
    r['units']=make(k);wx=pcm(root/f'{k}_W.m4a')
    for arm in ('KW','WaSFd','WaSF'):
        enc=root/f'{k}_H_{arm}.m4a'
        env=dict(os.environ,FAAC_STEP1=str(root/f'{k}_H_{arm}.bin'),FAAC_STEP1_OFFSET='1',FAAC_STEP1_ORIGIN=str(root/f'{k}_H_{arm}_origin.bin'))
        subprocess.run([faac,'--overwrite','-b','128','-o',str(enc),str(g/f'{k}_plus.wav')],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,check=True)
        x=pcm(enc)
        if arm=='KW':r['KW']={'pcm_ident':bool(np.array_equal(x,wx))};print(k,'KW',r['KW'],flush=True);continue
        wav=root/f'{k}_H_{arm}.wav';sf.write(wav,x[64:64+n],48000,subtype='FLOAT')
        r[arm]={'mos':score(src,wav),'bytes':enc.stat().st_size};wav.unlink();print(k,arm,r[arm],flush=True)
    res[k]=r;out.write_text(json.dumps(res,indent=1))
print('DONE')
