import os,subprocess,pathlib
root=pathlib.Path('/tmp/ladder_f')
for n in ('Severance','21classic','velvet','Greensleeves','German'):
 env=os.environ.copy();env.update(FAAD_DUMP=str(root/(n+'_KF_IS_BIAS.dump')),FAAD_LADDER_DUMP='1')
 subprocess.run(['/private/tmp/claude-501/faac-work/faad-dump/build-ladder/frontend/faad','-q','-o',str(root/(n+'_KF_IS_BIAS_faad.wav')),str(root/(n+'_KF_IS_BIAS.m4a'))],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,check=True)
