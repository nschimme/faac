import sys
import os,pathlib
_W=pathlib.Path(os.environ.get('LADDER_WORK','./ladder_work'));_REPO=pathlib.Path(__file__).resolve().parents[4]
LADDER_G=pathlib.Path(os.environ.get('LADDER_G',str(_W/'g')));LADDER_H=pathlib.Path(os.environ.get('LADDER_H',str(_W/'h')));LADDER_S=pathlib.Path(os.environ.get('LADDER_S',str(_W/'s')))
FAAC_BIN=os.environ.get('FAAC_BIN',str(_REPO/'build_ladder/frontend/faac'));FAAD_BIN=os.environ.get('FAAD_BIN','/tmp/faad-ladder-dump/build_faad/frontend/faad');SCORE_CLIP=os.environ.get('SCORE_CLIP','/opt/faac-benchmark/scripts/score_clip.py');PYTHON_BIN=os.environ.get('PYTHON_BIN',sys.executable)
FAAC_SMOOTH_BIN=os.environ.get('FAAC_SMOOTH_BIN','/home/user/wt/sf-smooth/build/frontend/faac');FAAC_MASTER_BIN=os.environ.get('FAAC_MASTER_BIN','/home/user/wt/master/build/frontend/faac')
# Stage H: Apple decisions injected into FAAC's normal encoder (rate loop live), +64 input, offset 2.
import os,subprocess,pathlib,json,re,numpy as np,soundfile as sf,sys
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'h'));from h_conv import conv
root=LADDER_H;g=LADDER_G
faac=FAAC_BIN
faad=FAAD_BIN
sc=SCORE_CLIP;py=PYTHON_BIN
index=json.loads((g/'g2_index.json').read_text())
out=root/'h_scores.json';res=json.loads(out.read_text()) if out.exists() else {}
ARMS=[('W','apple','win'),('S','apple','sf'),('WS','apple','win,sf'),('WSC','apple','win,sf,class'),('WSCM','apple','win,sf,class,ms'),
      ('selfW','normal','win'),('selfWS','normal','win,sf')]
def score(src,wav):
    p=subprocess.run([py,sc,str(src),str(wav)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True);m=re.search(r'MOS: ([\d.]+)',p.stdout)
    if not m:raise RuntimeError(p.stdout)
    return float(m.group(1))
def aligned(m4a,n,wav):
    raw=wav.with_suffix('.f32');subprocess.run(['ffmpeg','-v','error','-y','-i',str(m4a),'-f','f32le','-ac','2',str(raw)],check=True)
    x=np.fromfile(raw,dtype='<f4').reshape(-1,2);assert len(x)>=n+64;sf.write(wav,x[64:64+n],48000,subtype='FLOAT');raw.unlink()
for item in index:
    k=item['id']
    if k in res and all(a in res[k] for a,_,_ in ARMS)and 'N' in res[k]:continue
    src=pathlib.Path(item['src']);n=sf.info(src).frames;r=res.setdefault(k,{'stem':item['stem']})
    for tag in ('apple','normal'):
        ci=root/f'{k}_{tag}.ci'
        if not ci.exists():conv(str(g/f'{k}_{tag}.dump'),str(ci))
    nw=root/f'{k}_N.wav';aligned(g/f'{k}_normal.m4a',n,nw);r['N']={'mos':score(src,nw),'bytes':(g/f'{k}_normal.m4a').stat().st_size}
    for arm,tag,fl in ARMS:
        enc=root/f'{k}_{arm}.m4a';enc.unlink(missing_ok=True)
        env=dict(os.environ,FAAC_CORE_INJECT=str(root/f'{k}_{tag}.ci'),FAAC_CORE_INJECT_FIELDS=fl,FAAC_CORE_INJECT_OFFSET='2' if tag=='apple' else '1',FAAC_CORE_INJECT_LOOSE_SFB='1',FAAC_CORE_INJECT_DEBUG='1')
        p=subprocess.run([faac,'-b','128','-o',str(enc),str(g/f'{k}_plus.wav')],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        if p.returncode:raise RuntimeError(k+arm+p.stdout[-500:])
        m=re.search(r'sf: matched (\d+)',p.stdout)
        wav=root/f'{k}_{arm}.wav';aligned(enc,n,wav)
        if arm.startswith('self'):
            same=np.array_equal(sf.read(wav)[0],sf.read(nw)[0]);r[arm]={'pcm_ident':bool(same),'bytes':enc.stat().st_size}
            print(k,arm,'IDENT' if same else 'DIFF',flush=True)
        else:
            r[arm]={'mos':score(src,wav),'bytes':enc.stat().st_size,'sf_matched':int(m.group(1)) if m else 0};print(k,arm,r[arm],flush=True)
        wav.unlink()
    nw.unlink();out.write_text(json.dumps(res,indent=1))
print('DONE')
