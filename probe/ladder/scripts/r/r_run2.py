import os,subprocess,pathlib,json,re,numpy as np,soundfile as sf,sys
sys.path.insert(0,'/tmp/ladder_h');from r_make import make
root=pathlib.Path('/tmp/ladder_h');g=pathlib.Path('/tmp/ladder_g')
faac='/private/tmp/claude-501/faac-work/fdk-ladder/build-ladder/frontend/faac'
sc='/Users/nschimme/gitprojects/faac-benchmark/scripts/score_clip.py';py=sys.executable
idx=json.loads((g/'g2_index.json').read_text());out=root/'r2_scores.json';res=json.loads(out.read_text()) if out.exists() else {}
def pcm(m4a):
    raw=root/'rtmp.f32';subprocess.run(['ffmpeg','-v','error','-y','-i',str(m4a),'-f','f32le','-ac','2',str(raw)],check=True);x=np.fromfile(raw,dtype='<f4').reshape(-1,2);raw.unlink();return x
nx_cache={}
for ci,it in enumerate(idx):
    k=it['id']
    if k in res:continue
    src=pathlib.Path(it['src']);n=sf.info(src).frames;r={'units':make(ci,k,['K0','S3','S6','S9'])};nx=pcm(g/f'{k}_normal.m4a')
    for arm in ('K0','S3','S6','S9'):
        enc=root/f'{k}_R_{arm}.m4a'
        env=dict(os.environ,FAAC_STEP1=str(root/f'{k}_R_{arm}.bin'),FAAC_STEP1_OFFSET='1',FAAC_STEP1_ORIGIN=str(root/f'{k}_R_{arm}_origin.bin'))
        subprocess.run([faac,'--overwrite','-b','128','-o',str(enc),str(g/f'{k}_plus.wav')],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,check=True)
        x=pcm(enc)
        if arm=='K0':r['K0']=bool(np.array_equal(x,nx));print(k,'K0',r['K0'],flush=True);continue
        wav=root/f'{k}_R.wav';sf.write(wav,x[64:64+n],48000,subtype='FLOAT')
        p=subprocess.run([py,sc,str(src),str(wav)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True);m=re.search(r'MOS: ([\d.]+)',p.stdout)
        r[arm]={'mos':float(m.group(1)),'bytes':enc.stat().st_size};print(k,arm,r[arm],flush=True)
    res[k]=r;out.write_text(json.dumps(res,indent=1))
print('DONE')
