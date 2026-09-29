"""E: build step1 arms A, F, rSF, rWIN from a G2 prepare dir, plus core-inject W and its selfW control.
usage: e_make.py <G>   env: LADDER_RATE, FAAC_BIN (+ base knobs)"""
import os,sys,json,pathlib,subprocess,numpy as np
here=pathlib.Path(__file__).resolve().parent;sys.path.insert(0,str(here.parent/'g'));sys.path.insert(0,str(here.parent/'h'))
from g_make import make_one
from h_conv import conv
G=pathlib.Path(sys.argv[1]);rate=os.environ.get('LADDER_RATE','128');faac=os.environ['FAAC_BIN']
def pcm(m):return subprocess.run(['ffmpeg','-v','error','-i',str(m),'-f','f32le','-ac','2','-'],capture_output=True,check=True).stdout
res={}
for it in json.load(open(G/'g2_index.json')):
    k=it['id']
    if not (G/f'{k}_G_rWIN.bin').exists():make_one(k,it['stem'],G/f'{k}_apple.dump',G/f'{k}_normal.dump',['A','F','rSF','rWIN'])
    for tag in ('apple','normal'):
        ci=G/f'{k}_{tag}.ci'
        if not ci.exists():conv(str(G/f'{k}_{tag}.dump'),str(ci))
    r={}
    for arm,tag,off in (('W','apple','2'),('selfW','normal','1')):
        enc=G/f'{k}_H_{arm}.m4a';enc.unlink(missing_ok=True)
        env=dict(os.environ,FAAC_CORE_INJECT=str(G/f'{k}_{tag}.ci'),FAAC_CORE_INJECT_FIELDS='win',FAAC_CORE_INJECT_OFFSET=off,FAAC_CORE_INJECT_LOOSE_SFB='1')
        subprocess.run([faac,'-b',rate,'-o',str(enc),str(G/f'{k}_plus.wav')],env=env,capture_output=True,check=True)
    r['selfW']=pcm(G/f'{k}_H_selfW.m4a')==pcm(G/f'{k}_normal.m4a');(G/f'{k}_H_selfW.m4a').unlink()
    res[k]=r;print(k,it['stem'],r,flush=True)
json.dump(res,open(G/'e_make.json','w'),indent=0);print('selfW PCM-identical',sum(v['selfW'] for v in res.values()),'/',len(res))
