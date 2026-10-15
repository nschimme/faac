import subprocess,sys,os
S='/tmp/claude-0/-home-user-faac/bc9c8615-9277-5565-96c9-f1ff39d121b8/scratchpad'
sys.path.insert(0,S+'/fb'); from phase2_mos import score_wav_pair
for d,rate in (('audio_44k1',48),('audio_32k',40)):
    for c in ('cst.wav','SinceAlways.16b48k.wav'):
        ref=f'{S}/fb/data/external/{d}/{c}'
        for st in range(6,16):
            env=dict(os.environ,FAAC_PROBE_SBR_START=str(st))
            subprocess.run([S+'/exp/build/frontend/faac','--object-type','he-aac-v1','--overwrite','-b',str(rate),'-o','/tmp/s.m4a',ref],env=env,capture_output=True)
            subprocess.run([S+'/fb/bin/faad','-q','-o','/tmp/s1.wav','/tmp/s.m4a'],capture_output=True)
            subprocess.run(['ffmpeg','-v','error','-y','-i','/tmp/s.m4a','-sample_fmt','s16','/tmp/s2.wav'],capture_output=True)
            a=score_wav_pair(ref,'/tmp/s1.wav',mode_str='audio',sample_rate=48000)[0]; b=score_wav_pair(ref,'/tmp/s2.wav',mode_str='audio',sample_rate=48000)[0]
            print(d,c[:6],st,'faad2 %.2f ffmpeg %.2f %s'%(a,b,'<<DISAGREE' if abs(a-b)>0.15 else ''),flush=True)
