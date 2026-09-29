import os,subprocess,pathlib,sys
sys.path.insert(0,'probe/ladder');import parse_dump as pd
root=pathlib.Path('/tmp/ladder_f');repo=pathlib.Path.cwd();clips={'Severance':'Severance__1.31-1.51_.16b48k','21classic':'21-classic.441.16b48k','velvet':'velvet.16b48k','Greensleeves':'24-Greensleeves-Korean-male-speech.441.16b48k','German':'12-German-male-speech.441.16b48k'}
for name,stem in clips.items():
 out=root/(name+'_F1_normal.m4a');dump=root/(name+'_F1_normal.dump')
 with open(root/(name+'_F1_normal.log'),'w') as log:subprocess.run([str(repo/'build-ladder/frontend/faac'),'-b','128','-o',str(out),str(root/(name+'_plus.wav'))],stdout=log,stderr=subprocess.STDOUT,check=True)
 env=os.environ.copy();env.update(FAAD_DUMP=str(dump),FAAD_LADDER_DUMP='1')
 with open(root/(name+'_F1_decode.log'),'w') as log:subprocess.run(['/private/tmp/claude-501/faac-work/faad-dump/build-ladder/frontend/faad','-q','-o',str(root/(name+'_F1_faad.wav')),str(out)],env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
 subprocess.run(['python3',str(repo/'probe/ladder/parse_dump.py'),str(dump),str(root/(name+'_F1.bin'))],stdout=subprocess.DEVNULL,check=True)
 a=pd.parse(str(dump));b=pd.parse(str(root/(name+'_F0.dump')))
 scores=[]
 for off in (-1,0,1,2):
  match=total=0
  for af,ch in a.items():
   for k,ai in ch.items():
    bi=b.get(af+off,{}).get(k)
    if not bi:continue
    total+=1;match+=int((ai.win_seq,ai.window_shape,ai.num_groups,ai.group_len)==(bi.win_seq,bi.window_shape,bi.num_groups,bi.group_len))
  scores.append((off,match,total))
 print(name,'offset scores',scores,flush=True)
