import subprocess,sys,os
S='/tmp/claude-0/-home-user-faac/bc9c8615-9277-5565-96c9-f1ff39d121b8/scratchpad'
sys.path.insert(0,S+'/fb'); from phase2_mos import score_wav_pair
src={'s48':'audio','s441':'audio_44k1','s32':'audio_32k'}
for n in sorted(os.listdir(S+'/fx/streams')):
    n=n[:-4]; ref=f"{S}/fb/data/external/{src[n.split('_')[0]]}/cst.wav"; st=f'{S}/fx/streams/{n}.m4a'; out=[]
    for name,cmd,f in (('orig',[S+'/fx/orig/faad','-q','-o','/tmp/c1.wav',st],'/tmp/c1.wav'),('fixed',[S+'/fx/fixed/faad','-q','-o','/tmp/c2.wav',st],'/tmp/c2.wav'),('ffmpeg',['ffmpeg','-v','error','-y','-i',st,'-sample_fmt','s16','/tmp/c3.wav'],'/tmp/c3.wav'),('libfaad',[S+'/lf/build/frontend/faad','-o','/tmp/c4.wav',st],'/tmp/c4.wav')):
        if os.path.exists(f): os.remove(f)
        subprocess.run(cmd,capture_output=True); out.append('%s %.2f'%(name,score_wav_pair(ref,f,mode_str='audio',sample_rate=48000)[0]))
    print(n,' | '.join(out),flush=True)
