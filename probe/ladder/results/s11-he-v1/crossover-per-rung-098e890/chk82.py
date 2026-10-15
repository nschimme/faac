import os,random,subprocess,hashlib,sys,numpy as np
S='/tmp/claude-0/-home-user-faac/bc9c8615-9277-5565-96c9-f1ff39d121b8/scratchpad'
sys.path.insert(0,S+'/fb'); from phase2_mos import score_wav_pair
src={'out':'fb/data/external/audio','out_44k1':'fb/data/external/audio_44k1','out_32k':'fb/data/external/audio_32k','out_held':'held'}
fs=[]
for root,_,files in os.walk(S+'/run'):
    for f in files:
        if f.endswith('.m4a'): fs.append(os.path.join(root,f))
random.seed(1); random.shuffle(fs); fs=fs[:600]
def dec(b,f,o):
    if os.path.exists(o): os.remove(o)
    if b=='ffmpeg': subprocess.run(['ffmpeg','-v','error','-y','-i',f,'-sample_fmt','s16',o],capture_output=True)
    else: subprocess.run([f'{S}/fx/{b}/faad','-q','-o',o,f],capture_output=True)
    return hashlib.md5(open(o,'rb').read()).hexdigest() if os.path.exists(o) else None
rows=[]
for f in fs:
    ho=dec('orig',f,'/tmp/k1.wav'); hf=dec('fixed',f,'/tmp/k2.wav')
    if ho==hf: continue
    rel=f.replace(S+'/run/','').split('/'); clip='.'.join(rel[-1].split('.')[:-1]); ref=f"{S}/{src[rel[0]]}/{clip}"
    dec('ffmpeg',f,'/tmp/k3.wav')
    m=[score_wav_pair(ref,p,mode_str='audio',sample_rate=48000)[0] for p in ('/tmp/k1.wav','/tmp/k2.wav','/tmp/k3.wav')]
    rows.append((rel[0]+'/'+rel[1],rel[2],*m))
import collections
g=collections.defaultdict(list)
for r in rows: g[r[0]].append(r[2:])
for k,v in sorted(g.items()):
    v=np.array(v); print(f'{k:16s} n={len(v):2d} orig-ffmpeg mean|d|={np.abs(v[:,0]-v[:,2]).mean():.3f} fixed-ffmpeg mean|d|={np.abs(v[:,1]-v[:,2]).mean():.3f}  max|fixed-ffmpeg|={np.abs(v[:,1]-v[:,2]).max():.3f}')
