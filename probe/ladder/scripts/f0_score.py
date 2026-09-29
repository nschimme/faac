import sys,pathlib,os,subprocess,re,math,numpy as np,soundfile as sf
root=pathlib.Path('/tmp/ladder_f');repo=pathlib.Path.cwd();data=pathlib.Path('/Users/nschimme/gitprojects/faac-benchmark/data/external/audio');sc='/Users/nschimme/gitprojects/faac-benchmark/scripts/score_clip.py';py='/Users/nschimme/gitprojects/faac-benchmark/.venv/bin/python'
clips={'Severance':'Severance__1.31-1.51_.16b48k','21classic':'21-classic.441.16b48k','velvet':'velvet.16b48k','Greensleeves':'24-Greensleeves-Korean-male-speech.441.16b48k','German':'12-German-male-speech.441.16b48k'}
def score(src,deg):
 env=os.environ.copy();env['PYTHONPATH']=str(root);p=subprocess.run([py,sc,str(src),str(deg)],env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,check=True);return float(re.search(r'MOS: ([\d.]+)',p.stdout).group(1))
for name,stem in clips.items():
 src=data/(stem+'.wav');n=sf.info(src).frames;enc=root/(name+'_F0.m4a');raw=root/(name+'_F0.f32');wav=root/(name+'_F0_aligned.wav')
 subprocess.run(['ffmpeg','-v','error','-y','-i',str(enc),'-f','f32le','-acodec','pcm_f32le','-ac','2','-ar','48000',str(raw)],check=True)
 x=np.fromfile(raw,dtype='<f4').reshape(-1,2);assert len(x)>=n+64;sf.write(wav,x[64:64+n],48000,subtype='FLOAT')
 items={'faac112':pathlib.Path('/tmp/ladder_c')/(name+'_faac112.m4a'),'faac128':pathlib.Path('/tmp/ladder_c')/(name+'_faac128.m4a'),'faac144':pathlib.Path('/tmp/ladder_c')/(name+'_faac144.m4a'),'apple':repo/'probe/ladder/ref/apple'/(stem+'.m4a'),'A':wav}
 for label,deg in items.items():print(name,label,score(src,deg),enc.stat().st_size if label=='A' else deg.stat().st_size,flush=True)
