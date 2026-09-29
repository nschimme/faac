import sys
import os,pathlib
_W=pathlib.Path(os.environ.get('LADDER_WORK','./ladder_work'));_REPO=pathlib.Path(__file__).resolve().parents[4]
LADDER_G=pathlib.Path(os.environ.get('LADDER_G',str(_W/'g')));LADDER_H=pathlib.Path(os.environ.get('LADDER_H',str(_W/'h')));LADDER_S=pathlib.Path(os.environ.get('LADDER_S',str(_W/'s')))
FAAC_BIN=os.environ.get('FAAC_BIN',str(_REPO/'build_ladder/frontend/faac'));FAAD_BIN=os.environ.get('FAAD_BIN','/tmp/faad-ladder-dump/build_faad/frontend/faad');SCORE_CLIP=os.environ.get('SCORE_CLIP','/opt/faac-benchmark/scripts/score_clip.py');PYTHON_BIN=os.environ.get('PYTHON_BIN',sys.executable)
FAAC_SMOOTH_BIN=os.environ.get('FAAC_SMOOTH_BIN','/home/user/wt/sf-smooth/build/frontend/faac');FAAC_MASTER_BIN=os.environ.get('FAAC_MASTER_BIN','/home/user/wt/master/build/frontend/faac')
# sf-smooth in FAAC's normal encoder (rate loop on), 49 clips, native input.
import os,subprocess,pathlib,json,re,sys
root=LADDER_S;base=FAAC_MASTER_BIN;new=FAAC_SMOOTH_BIN
sc=SCORE_CLIP;py=PYTHON_BIN
idx=json.load(open(LADDER_G/'g2_index.json'));out=root/'s_scores.json';res=json.loads(out.read_text()) if out.exists() else {}
ARMS=[('b112',base,112,None),('b128',base,128,None),('b144',base,144,None),('a3',new,128,'0.3'),('a6',new,128,'0.6'),('a9',new,128,'0.9')]
for it in idx:
    k=it['id']
    if k in res:continue
    r={'stem':it['stem']}
    for arm,exe,rate,al in ARMS:
        enc=root/f'{k}_{arm}.m4a';enc.unlink(missing_ok=True);env=dict(os.environ)
        if al:env['FAAC_SF_SMOOTH']=al
        subprocess.run([exe,'-b',str(rate),'-o',str(enc),it['src']],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,check=True)
        p=subprocess.run([py,sc,it['src'],str(enc)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True);m=re.search(r'MOS: ([\d.]+)',p.stdout)
        r[arm]={'mos':float(m.group(1)),'bytes':enc.stat().st_size};print(k,arm,r[arm],flush=True)
    res[k]=r;out.write_text(json.dumps(res,indent=1))
print('DONE')
