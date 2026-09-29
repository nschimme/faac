# sf-smooth in FAAC's normal encoder (rate loop on), 49 clips, native input.
import os,subprocess,pathlib,json,re,sys
root=pathlib.Path('/tmp/ladder_s');wt=pathlib.Path('/private/tmp/claude-501/faac-work/sf-smooth')
base=str(wt/'build-base/frontend/faac');new=str(wt/'build/frontend/faac')
sc='/Users/nschimme/gitprojects/faac-benchmark/scripts/score_clip.py';py=sys.executable
idx=json.load(open('/tmp/ladder_g/g2_index.json'));out=root/'s_scores.json';res=json.loads(out.read_text()) if out.exists() else {}
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
