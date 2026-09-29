import os,subprocess,pathlib,numpy as np
root=pathlib.Path('/tmp/ladder_f');repo=pathlib.Path.cwd();names=['Severance','21classic','velvet','Greensleeves','German']
def enc(name,tag,binfile,off):
 out=root/(name+'_'+tag+'.m4a');env=os.environ.copy();env.update(FAAC_STEP1=str(binfile),FAAC_STEP1_OFFSET=str(off))
 with open(root/(name+'_'+tag+'.log'),'w') as log:subprocess.run([str(repo/'build-ladder/frontend/faac'),'-b','128','-o',str(out),str(root/(name+'_plus.wav'))],env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
 return out
def adts(aac,raw):subprocess.run(['/private/tmp/claude-501/faac-work/faad-dump/build-ladder/frontend/faad','-a',str(raw),str(aac)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,check=True)
def pcm(aac,raw):subprocess.run(['ffmpeg','-v','error','-y','-i',str(aac),'-f','f32le','-acodec','pcm_f32le','-ac','2','-ar','48000',str(raw)],check=True)
for name in names:
 ka=enc(name,'KA',pathlib.Path('/tmp/ladder_c')/(name+'_apple.bin'),1);base=root/(name+'_F0.m4a')
 kf=enc(name,'KF',root/(name+'_F1.bin'),0);norm=root/(name+'_F1_normal.m4a')
 for label,x,y in [('KA',ka,base),('KF',kf,norm)]:
  xr=root/(name+'_'+label+'.aac');yr=root/(name+'_'+label+'_target.aac');adts(x,xr);adts(y,yr)
  xp=root/(name+'_'+label+'.f32');yp=root/(name+'_'+label+'_target.f32');pcm(x,xp);pcm(y,yp)
  a=np.fromfile(xp,dtype='<f4');b=np.fromfile(yp,dtype='<f4');n=min(len(a),len(b));d=np.abs(a[:n]-b[:n]);where=np.flatnonzero(d)
  print(name,label,'ADTS_equal',xr.read_bytes()==yr.read_bytes(),'max_abs',float(np.max(d)),'first_sample',int(where[0]//2) if len(where) else None,'first_frame',int(where[0]//2048) if len(where) else None,'diff_float_values',len(where),'samples',n//2,flush=True)
