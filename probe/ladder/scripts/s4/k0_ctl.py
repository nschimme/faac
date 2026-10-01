"""K0 control: step1 encode of <arm> (default K0) is PCM-identical to FAAC's own normal encode. usage: k0_ctl.py <G> [arm]"""
import os,sys,json,subprocess,pathlib
G=pathlib.Path(sys.argv[1]);arm=sys.argv[2] if len(sys.argv)>2 else 'K0';rate=os.environ.get('LADDER_RATE','128');faac=os.environ['FAAC_BIN']
def pcm(m):return subprocess.run(['ffmpeg','-v','error','-i',str(m),'-f','s16le','-'],capture_output=True,check=True).stdout
ok=[];bad=[]
for it in json.load(open(G/'g2_index.json')):
    if os.environ.get('LADDER_CLIP') and it['stem']!=os.environ['LADDER_CLIP']:continue
    k=it['id'];o=G/f'{k}_k0.m4a';o.unlink(missing_ok=True)
    env=dict(os.environ,FAAC_STEP1=str(G/f'{k}_G_{arm}.bin'),FAAC_STEP1_ORIGIN=str(G/f'{k}_G_{arm}_origin.bin'),FAAC_STEP1_OFFSET='1')
    subprocess.run([faac,'-b',rate,'-o',str(o),str(G/f'{k}_plus.wav')],env=env,capture_output=True,check=True)
    (ok if pcm(o)==pcm(G/f'{k}_normal.m4a') else bad).append(it['stem']);o.unlink()
print(f'{arm} PCM-identical to normal: {len(ok)}/{len(ok)+len(bad)}',bad)
if bad:raise SystemExit(1)
