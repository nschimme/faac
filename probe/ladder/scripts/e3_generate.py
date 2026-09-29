from score_common import *
for ref in ('apple','fdk'):
 for name,stem in clips.items():
  dump=root/(name+('_aligned' if ref=='apple' else '_fdk')+'.dump');refdump=repo/'probe/ladder/survey'/ref/(stem+'.dump')
  for arm in ('Z','S','ZS','M','LO','HI'):
   prefix=root/(name+'_'+ref+'_'+arm)
   with open(str(prefix)+'.merge.log','w') as log:run([venv,str(repo/'probe/ladder/hybrid_merge.py'),arm,str(dump),str(refdump),str(prefix)+'.bin'],stdout=log)
   run([str(repo/'probe/ladder/reemit_tool'),str(prefix)+'.bin',str(prefix)+'.aac'],stderr=subprocess.DEVNULL)
  print(name,ref,'generated',flush=True)
