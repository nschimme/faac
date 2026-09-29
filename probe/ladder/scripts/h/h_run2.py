import os,subprocess,pathlib,json,re,numpy as np,soundfile as sf,sys
sys.path.insert(0,'/tmp/ladder_h');from h_make import make
root=pathlib.Path('/tmp/ladder_h');g=pathlib.Path('/tmp/ladder_g')
faac='/private/tmp/claude-501/faac-work/fdk-ladder/build-ladder/frontend/faac'
faad='/private/tmp/claude-501/faac-work/faad-dump/build-ladder/frontend/faad'
sc='/Users/nschimme/gitprojects/faac-benchmark/scripts/score_clip.py';py=sys.executable
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
