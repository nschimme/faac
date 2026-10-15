import subprocess,os,sys
S='/tmp/claude-0/-home-user-faac/bc9c8615-9277-5565-96c9-f1ff39d121b8/scratchpad'
sys.path.insert(0,S+'/fb'); from phase2_mos import score_wav_pair
cases=[('audio',48,(9,10,11,15)),('audio_44k1',48,(7,10,11,15)),('audio_32k',40,(7,8,11,15))]
for d,rate,sts in cases:
    ref=f'{S}/fb/data/external/{d}/cst.wav'
    for st in sts:
        env=dict(os.environ,FAAC_PROBE_SBR_START=str(st))
        subprocess.run([S+'/exp/build/frontend/faac','--object-type','he-aac-v1','--overwrite','-b',str(rate),'-o','/tmp/q.m4a',ref],env=env,capture_output=True)
        out=[]
        for name,cmd in (('faad2-287c00c',[S+'/fb/bin/faad','-q','-o','/tmp/q1.wav','/tmp/q.m4a']),('libfaad-branch',[S+'/lf/build/frontend/faad','-o','/tmp/q2.wav','/tmp/q.m4a'])):
            f=cmd[3] if name.startswith('faad2') else '/tmp/q2.wav'
            if os.path.exists(f): os.remove(f)
            subprocess.run(cmd,capture_output=True)
            out.append('%s %.2f'%(name,score_wav_pair(ref,f,mode_str='audio',sample_rate=48000)[0]) if os.path.exists(f) else name+' FAILED')
        print(d,'start',st,' | '.join(out),flush=True)
