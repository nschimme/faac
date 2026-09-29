import os,subprocess,pathlib,json
root=pathlib.Path('/tmp/ladder_g');repo=pathlib.Path.cwd();faac=str(repo/'build-ladder/frontend/faac');faad='/private/tmp/claude-501/faac-work/faad-dump/build-ladder/frontend/faad';index=json.loads((root/'g2_index.json').read_text());status={}
for item in index:
 k=item['id'];status[k]={}
 for tag,extra,target in [('spec',{'FAAC_STEP1':str(root/(k+'_G_A.bin')),'FAAC_STEP1_OFFSET':'1','FAAC_STEP1_ORIGIN':str(root/(k+'_G_A_origin.bin')),'FAAC_STEP1_SPEC_DUMP':str(root/(k+'_G_spec.txt'))},root/(k+'_A.aac')),('mask',{'FAAC_G_MASK_DUMP':str(root/(k+'_G_mask.txt'))},root/(k+'_normal.aac'))]:
  dump=root/(k+'_G_'+tag+'.txt');dump.unlink(missing_ok=True);out=root/(k+'_G_'+tag+'.m4a');env=os.environ.copy();env.update(extra)
  with open(root/(k+'_G_'+tag+'.log'),'w') as log:p=subprocess.run([faac,'--overwrite','-b','128','-o',str(out),str(root/(k+'_plus.wav'))],env=env,stdout=log,stderr=subprocess.STDOUT)
  if p.returncode:status[k][tag]=False;print(k,tag,'ENC_FAIL',flush=True);continue
  raw=root/(k+'_G_'+tag+'.aac');q=subprocess.run([faad,'-a',str(raw),str(out)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
  same=q.returncode==0 and raw.read_bytes()==target.read_bytes();status[k][tag]=same;print(k,tag,'ADTS',same,'lines',sum(1 for _ in dump.open()) if dump.exists() else 0,flush=True)
 (root/'g3_wide_dumps.json').write_text(json.dumps(status,indent=2))
