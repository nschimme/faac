# Control 0 for any LC reference set: reference dump -> parse_dump -> reemit_tool (ADTS)
# must decode to the reference's own decoded PCM after its priming (2112 for Apple, 2048 fdk).
# Env: LADDER_WORK/LADDER_G (needs g2_prepare output: g2_index.json, *_apple.dump), LADDER_PRIME.
import os,sys,json,pathlib,subprocess,numpy as np
repo=pathlib.Path(__file__).resolve().parents[4]
G=pathlib.Path(os.environ.get('LADDER_G',str(pathlib.Path(os.environ.get('LADDER_WORK','./ladder_work'))/'g')))
prime=int(os.environ.get('LADDER_PRIME','2112'))
def pcm(p):
    r=G/'c0tmp.f32';subprocess.run(['ffmpeg','-v','error','-y','-i',str(p),'-f','f32le','-ac','2',str(r)],check=True)
    x=np.fromfile(r,'<f4').reshape(-1,2);r.unlink();return x
res={};ok=0
for it in json.load(open(G/'g2_index.json')):
    k=it['id'];b=G/f'{k}_c0.bin';a=G/f'{k}_c0.aac'
    subprocess.run([sys.executable,str(repo/'probe/ladder/parse_dump.py'),str(G/f'{k}_apple.dump'),str(b)],check=True,capture_output=True)
    subprocess.run([str(repo/'probe/ladder/reemit_tool'),str(b),str(a)],check=True,capture_output=True)
    x=pcm(a)[prime:];y=pcm(it['ref']);n=min(len(x),len(y))
    d=int(np.count_nonzero(x[:n]!=y[:n]));res[k]={'stem':it['stem'],'diff_samples':d,'n':n};ok+=d==0
    print(k,'PASS' if d==0 else f'FAIL {d} differing',flush=True)
json.dump(res,open(G/'control0.json','w'),indent=1);print(f'Control 0: {ok}/{len(res)} exact')
