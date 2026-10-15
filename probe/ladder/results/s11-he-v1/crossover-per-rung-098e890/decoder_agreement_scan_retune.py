import subprocess,sys,os
S='/tmp/claude-0/-home-user-faac/bc9c8615-9277-5565-96c9-f1ff39d121b8/scratchpad'
sys.path.insert(0,S+'/fb'); from phase2_mos import score_wav_pair
def sc(ref,f): return score_wav_pair(ref,f,mode_str='audio',sample_rate=48000)[0] if os.path.exists(f) else float('nan')
for d,rate in (('audio',48),('audio_44k1',48),('audio_32k',40)):
    for c in ('cst.wav','SinceAlways.16b48k.wav'):
        ref=f'{S}/fb/data/external/{d}/{c}'
        for nocap in (0,1):
            for st in (9,10,11,12,13):
                env=dict(os.environ,FAAC_PROBE_SBR_START=str(st)); 
                if nocap: env['FAAC_PROBE_NOCAP']='1'
                subprocess.run([S+'/x2/build/frontend/faac','--object-type','he-aac-v1','--overwrite','-b',str(rate),'-o','/tmp/z.m4a',ref],env=env,capture_output=True)
                res=[]
                for n,cmd,f in (('faad2',[S+'/fb/bin/faad','-q','-o','/tmp/z1.wav','/tmp/z.m4a'],'/tmp/z1.wav'),('libfaad',[S+'/lf/build/frontend/faad','-o','/tmp/z2.wav','/tmp/z.m4a'],'/tmp/z2.wav'),('ffmpeg',['ffmpeg','-v','error','-y','-i','/tmp/z.m4a','-sample_fmt','s16','/tmp/z3.wav'],'/tmp/z3.wav'),('fdk',[S+'/fb/bin/fdkdec','/tmp/z.m4a','/tmp/z4.wav'],'/tmp/z4.wav')):
                    if os.path.exists(f): os.remove(f)
                    subprocess.run(cmd,capture_output=True); res.append(sc(ref,f))
                sz=os.path.getsize('/tmp/z.m4a')
                flag='' if max(res)-min(res)<0.15 else ' <<DISAGREE'
                print(d,c[:5],'cap-lifted' if nocap else 'capped    ',st,' '.join('%s %.2f'%(n,v) for n,v in zip(('faad2','libfaad','ffmpeg','fdk'),res)),sz,flag,flush=True)
