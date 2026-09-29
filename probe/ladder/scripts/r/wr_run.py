import sys
import os,pathlib
_W=pathlib.Path(os.environ.get('LADDER_WORK','./ladder_work'));_REPO=pathlib.Path(__file__).resolve().parents[4]
LADDER_G=pathlib.Path(os.environ.get('LADDER_G',str(_W/'g')));LADDER_H=pathlib.Path(os.environ.get('LADDER_H',str(_W/'h')));LADDER_S=pathlib.Path(os.environ.get('LADDER_S',str(_W/'s')))
FAAC_BIN=os.environ.get('FAAC_BIN',str(_REPO/'build_ladder/frontend/faac'));FAAD_BIN=os.environ.get('FAAD_BIN','/tmp/faad-ladder-dump/build_faad/frontend/faad');SCORE_CLIP=os.environ.get('SCORE_CLIP','/opt/faac-benchmark/scripts/score_clip.py');PYTHON_BIN=os.environ.get('PYTHON_BIN',sys.executable)
FAAC_SMOOTH_BIN=os.environ.get('FAAC_SMOOTH_BIN','/home/user/wt/sf-smooth/build/frontend/faac');FAAC_MASTER_BIN=os.environ.get('FAAC_MASTER_BIN','/home/user/wt/master/build/frontend/faac')
import os,subprocess,pathlib,json,re,numpy as np,soundfile as sf,sys
root=LADDER_H;g=LADDER_G
faac=os.environ.get('FAAC_SMOOTH_BIN',FAAC_BIN)  # needs the probe binary: FAAC_SF_SMOOTH + FAAC_CORE_INJECT hooks
sc=SCORE_CLIP;py=PYTHON_BIN
index=json.loads((g/'g2_index.json').read_text())
out=root/'wr_scores.json';res=json.loads(out.read_text()) if out.exists() else {}
def score(src,wav):
    p=subprocess.run([py,sc,str(src),str(wav)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True);m=re.search(r'MOS: ([\d.]+)',p.stdout)
    if not m:raise RuntimeError(p.stdout)
    return float(m.group(1))
def dec(m4a):
    return np.frombuffer(subprocess.run(['ffmpeg','-v','error','-i',str(m4a),'-f','f32le','-ac','2','-'],check=True,stdout=subprocess.PIPE).stdout,dtype='<f4')
def aligned(m4a,n,wav):
    x=dec(m4a).reshape(-1,2);assert len(x)>=n+64;sf.write(wav,x[64:64+n],48000,subtype='FLOAT')
def enc(k,out,smooth,inject):
    out.unlink(missing_ok=True)
    env={kk:v for kk,v in os.environ.items() if not kk.startswith('FAAC_')};env['FAAC_SF_SMOOTH']=smooth
    if inject:env.update(FAAC_CORE_INJECT=str(root/f'{k}_apple.ci'),FAAC_CORE_INJECT_FIELDS='win',FAAC_CORE_INJECT_OFFSET='2',FAAC_CORE_INJECT_LOOSE_SFB='1')
    p=subprocess.run([faac,'-b','128','-o',str(out),str(g/f'{k}_plus.wav')],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    if p.returncode:raise RuntimeError(k+p.stdout[-500:])
tmp=root/'wr_tmp';tmp.mkdir(exist_ok=True)
# controls
for item in index:
    k=item['id'];r=res.setdefault(k,{'stem':item['stem']})
    if 'C1' in r:continue
    enc(k,tmp/'c1.m4a','0',True);r['C1']=bool(np.array_equal(dec(tmp/'c1.m4a'),dec(root/f'{k}_W.m4a')))
    enc(k,tmp/'c2.m4a','0',False);r['C2']=bool(np.array_equal(dec(tmp/'c2.m4a'),dec(g/f'{k}_normal.m4a')))
    print(k,'C1',r['C1'],'C2',r['C2'],flush=True);out.write_text(json.dumps(res,indent=1))
c1=sum(r['C1'] for r in res.values());c2=sum(r['C2'] for r in res.values());print('CONTROLS C1',c1,'C2',c2,flush=True)
if c1<len(index) or c2<len(index):print('CONTROL FAIL');sys.exit(1)
for item in index:
    k=item['id'];r=res[k]
    if 'NR' in r and 'WR' in r:continue
    src=pathlib.Path(item['src']);n=sf.info(src).frames
    for arm,inj in (('NR',False),('WR',True)):
        m=tmp/f'{k}_{arm}.m4a';enc(k,m,'0.6',inj);w=tmp/f'{k}_{arm}.wav';aligned(m,n,w)
        r[arm]={'mos':score(src,w),'bytes':m.stat().st_size};print(k,arm,r[arm],flush=True);w.unlink()
    out.write_text(json.dumps(res,indent=1))
print('DONE')
