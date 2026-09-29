import os,subprocess,pathlib
root=pathlib.Path('/tmp/ladder_f')
for n in ('Severance','21classic','velvet','Greensleeves','German'):
 env=os.environ.copy();env.update(FAAD_DUMP=str(root/(n+'_KF_BIAS2.dump')),FAAD_LADDER_DUMP='1')
 subprocess.run(['/private/tmp/claude-501/faac-work/faad-dump/build-ladder/frontend/faad','-q','-o',str(root/(n+'_KF_BIAS2_faad.wav')),str(root/(n+'_KF_BIAS2.m4a'))],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,check=True)
