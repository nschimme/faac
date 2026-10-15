#!/usr/bin/env python3
"""Validate the fixed-8 transient split on the upstream-01e7 branch and PR68."""
import concurrent.futures, hashlib, json, os, subprocess, sys, tempfile, threading, wave
from pathlib import Path

ROOT=Path('/Users/nschimme/gitprojects/faac')
BENCH=Path('/Users/nschimme/gitprojects/faac-benchmark')
OUT=ROOT/'.cache/he-v1-feature-study/transient-border/upstream-fixed8-validation'
FULL=ROOT/'.cache/he-v1-feature-study/transient-border/fixed8-full-v2/rows.json'
BASE_ROWS=Path('/private/tmp/faac-he-v1-probes/probe/results-all/feature-results.json')
STACK_ROWS=ROOT/'.cache/he-v1-feature-study/stack/results-all/stack-results.json'
ENVCNT_ROWS=ROOT/'.cache/he-v1-feature-study/envelope-count/full-active-lambda/envcount-results.json'
FAAD=Path('/private/tmp/faac-he-v1-decoder-build/frontend/faad')
FFMPEG=Path('/opt/homebrew/bin/ffmpeg')
BASE={'repo':Path('/private/tmp/faac-pr-fixed-border8'),'bin':Path('/private/tmp/faac-pr-fixed-border8/build-border/frontend/faac'),'libfaac':Path('/private/tmp/faac-pr-fixed-border8/build-border/libfaac/libfaac.3.dylib'),'libfaam':Path('/private/tmp/faac-pr-fixed-border8/build-border/libfaam/libfaam.3.dylib')}
COMBO={'repo':Path('/private/tmp/faac-pr-combined-border8'),'bin':Path('/private/tmp/faac-pr-combined-border8/build-border/frontend/faac'),'libfaac':Path('/private/tmp/faac-pr-combined-border8/build-border/libfaac/libfaac.3.dylib'),'libfaam':Path('/private/tmp/faac-pr-combined-border8/build-border/libfaam/libfaam.3.dylib')}
GATE=('sandman.16b48k.wav','velvet.16b48k.wav','21-classic.441.16b48k.wav','fms.wav')
RATES=(32,48,64,96)
UPSTREAM='01e7aa7f078095a69e8423578f8abab8ae61ec70'

def sha(p):
 h=hashlib.sha256();
 with Path(p).open('rb') as f:
  for b in iter(lambda:f.read(1<<20),b''):h.update(b)
 return h.hexdigest()
def run(cmd,env=None): return subprocess.run(cmd,check=True,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,env=env)
def packet_sha(p):
 x=run(['ffprobe','-v','error','-select_streams','a:0','-show_packets','-show_data_hash','sha256','-show_entries','packet=size,data_hash','-of','json',str(p)])
 items=json.loads(x.stdout).get('packets',[])
 if not items: raise RuntimeError(f'no packets: {p}')
 h=hashlib.sha256()
 for q in items:h.update(str(int(q['size'])).encode()+b':'+q['data_hash'].encode()+b'\n')
 return h.hexdigest()
def prof(p):
 x=run(['ffprobe','-v','error','-select_streams','a:0','-show_entries','stream=profile','-of','json',str(p)])
 q=json.loads(x.stdout).get('streams',[])
 if len(q)!=1:return ''
 return q[0].get('profile','')
def duration(p):
 with wave.open(str(p),'rb') as w:return w.getnframes()/w.getframerate()
def patch_id(repo):
 h=subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'],text=True).strip()
 commits=subprocess.check_output(['git','-C',str(repo),'rev-list','--reverse',UPSTREAM+'..HEAD'],text=True).splitlines()
 ids=[]
 for c in commits:
  patch=subprocess.run(['git','-C',str(repo),'show','--pretty=format:','--no-ext-diff',c],check=True,stdout=subprocess.PIPE).stdout
  out=subprocess.run(['git','patch-id','--stable'],input=patch,check=True,stdout=subprocess.PIPE).stdout.decode().split()
  ids.append({'commit':c,'patch_id':out[0] if out else None})
 return h,ids
def env_clean():
 e=dict(os.environ)
 for k in list(e):
  if k.startswith('FAAC_SBR_'):e.pop(k)
 return e

def main():
 sys.path.insert(0,str(BENCH))
 from codec_bench.encoders import FAACEncoder
 from codec_bench.decoders import FDKDecoder
 import utils
 audio=BENCH/'data/external/audio'
 full=json.load(open(FULL))['rows']; oldbase=json.load(open(BASE_ROWS))['rows']; oldstack=json.load(open(STACK_ROWS))['rows']; envrows=json.load(open(ENVCNT_ROWS))['rows']
 out=OUT; out.mkdir(parents=True,exist_ok=True); artifacts=out/'artifacts'; artifacts.mkdir(exist_ok=True)
 groups={'base_fixed8':BASE,'combo_fixed8':COMBO}; refs={}
 meta={}
 for name,cfg in groups.items():
  head,pids=patch_id(cfg['repo']);
  meta[name]={'repo':str(cfg['repo']),'head':head,'base':UPSTREAM if name=='base_fixed8' else 'b29d65ca12ddd25933fe33e54c2f15a44b4918d5','patch_ids':pids,'source_diff_sha256':hashlib.sha256(subprocess.check_output(['git','-C',str(cfg['repo']),'diff','--binary',UPSTREAM+'...HEAD'])).hexdigest(),'binary':str(cfg['bin']),'binary_sha256':sha(cfg['bin']),'libfaac_sha256':sha(cfg['libfaac']),'libfaam_sha256':sha(cfg['libfaam'])}
 # Fixed-8 candidate base: complete 49 x four ladder identity to measured fixed8_base.
 jobs=[('base_fixed8',c,r) for c in sorted({x['clip'] for x in full.values() if x['variant']=='fixed8_base'}) for r in RATES]
 # Combined PR68 extension: gate-only paired baseline/reference and combined candidate contexts.
 jobs += [('combo_fixed8',c,r) for c in GATE for r in RATES]
 results={}; lock=threading.Lock(); fdk=FDKDecoder('FDK AAC strict',str(BENCH/'bin/fdkdec'))
 def ref_for(group,clip,rate):
  if group=='base_fixed8':
   fixed=full[f'fixed8_base|{clip}|{rate}']; baseline=oldbase[f'baseline_default|{clip}|{rate}']
   return {'fixed8_validation':fixed,'quality_reference':baseline}
  variant='stack_reference' if rate==32 else 'all_stack'
  return {'quality_reference':envrows[f'{variant}|{clip}|{rate}']}
 def one(task):
  group,clip,rate=task; cfg=groups[group]; src=audio/clip
  enc=artifacts/group/str(rate)/(Path(clip).stem+'.m4a'); tmpwav=artifacts/group/str(rate)/(Path(clip).stem+'.fdk-source.wav')
  keep=(group=='combo_fixed8') or clip in GATE
  wav=(artifacts/group/str(rate)/(Path(clip).stem+'.wav')) if keep else Path(tempfile.gettempdir())/(f'faac-fixed8-{group}-{rate}-{Path(clip).stem}.wav')
  enc.parent.mkdir(parents=True,exist_ok=True); wav.parent.mkdir(parents=True,exist_ok=True)
  encoder=FAACEncoder('faac-fixed-transient-border',str(cfg['bin']),profile='he');cmd=encoder.get_encode_cmd(str(src),str(enc),rate,2,48000)
  if '--object-type' not in cmd or cmd[cmd.index('--object-type')+1]!='he-aac-v1':raise RuntimeError(f'not forced HEv1 {cmd}')
  proc=run(cmd,env=env_clean()); profile=prof(enc)
  if profile!='HE-AAC':raise RuntimeError(f'{task} ffprobe profile {profile}')
  de=env_clean();de.update(fdk.get_run_env());run(fdk.get_decode_cmd(str(enc),str(tmpwav)),env=de)
  if not utils.wav_conv(str(tmpwav),str(wav),rate=48000,channels=2):raise RuntimeError(f'PCM conversion failed: {task}')
  pcm=sha(wav); payload=packet_sha(enc)
  if group=='base_fixed8':
   expected=full[f'fixed8_base|{clip}|{rate}']
   if payload!=expected['payload_sha256'] or pcm!=expected['decoded_sha256']:
    status='mismatch'
   else:status='pass'
  else:status='measured_candidate'
  # Strict independent interoperability acceptance for every new candidate stream.
  faad_out=wav.parent/(wav.stem+'.faad.wav')
  faad=run([str(FAAD),'--strict','--overwrite','-o',str(faad_out),str(enc)])
  ff=run([str(FFMPEG),'-v','error','-xerror','-i',str(enc),'-f','null','-'])
  faad_pass=faad.returncode==0 and faad_out.is_file(); faad_out.unlink(missing_ok=True)
  ff_pass=ff.returncode==0 and not ff.stderr.strip()
  with wave.open(str(src),'rb') as w:dur=w.getnframes()/w.getframerate()
  pp=run(['ffprobe','-v','error','-select_streams','a:0','-show_packets','-show_entries','packet=size','-of','json',str(enc)])
  kbps=sum(int(x['size']) for x in json.loads(pp.stdout)['packets'])*8.0/dur/1000.0
  refs=ref_for(group,clip,rate)
  qref=refs['quality_reference']
  row={'variant':group,'clip':clip,'target_kbps':rate,'status':status,'profile':profile,'actual_payload_kbps':kbps,'payload_sha256':payload,'decoded_sha256':pcm,'reference':{k:v for k,v in qref.items() if k in ('mos','actual_payload_kbps','payload_sha256','decoded_sha256','profile','mos_source','variant','encoded_path','decoded_path','encoded_sha256')},'quality_reference_variant':qref.get('variant'),'mos_source':'pending_quality_score','faad_strict_pass':faad_pass,'ffmpeg_strict_pass':ff_pass,'encoded_path':str(enc),'encoded_sha256':sha(enc),'decoded_path':str(wav) if keep else None,'decoded_file_sha256':pcm,'command':cmd}
  if group=='base_fixed8':
   row['fixed8_measured_reference']={'packet_match':payload==refs['fixed8_validation']['payload_sha256'],'pcm_match':pcm==refs['fixed8_validation']['decoded_sha256']}
   # Baseline quality reference row is retained for the gate report.
   row['mos_inheritance_eligible_vs_fixed8_measured']=row['fixed8_measured_reference']['packet_match'] and row['fixed8_measured_reference']['pcm_match']
  with lock:
   results[f'{group}|{clip}|{rate}']=row
   tmp=out/'validation.json.tmp';tmp.write_text(json.dumps({'schema':1,'groups':meta,'source_base':UPSTREAM,'clips':sorted({x[1] for x in jobs}),'rates_kbps':list(RATES),'rows':results},indent=2,sort_keys=True)+'\n');os.replace(tmp,out/'validation.json')
  if not keep:enc.unlink(missing_ok=True);wav.unlink(missing_ok=True)
  tmpwav.unlink(missing_ok=True)
  print(f'{status} {group}|{clip}|{rate} packet={payload} faad={faad_pass} ffmpeg={ff_pass}',flush=True)
 def job_wrap(t):
  try:one(t)
  except Exception as e:
   with lock:results[f'ERROR|{t[0]}|{t[1]}|{t[2]}']={'error':repr(e)}
   print('ERROR',t,repr(e),flush=True)
 with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:list(pool.map(job_wrap,jobs))
 # Separate fresh decimation-4 build/smoke, built by this script's sibling step.
 summary={'base_fixed8_rows':sum(x.get('variant')=='base_fixed8' for x in results.values()),'base_fixed8_exact':sum(x.get('variant')=='base_fixed8' and x.get('status')=='pass' for x in results.values()),'combo_fixed8_gate_rows':sum(x.get('variant')=='combo_fixed8' for x in results.values()),'faad_pass':sum(x.get('faad_strict_pass',False) for x in results.values()),'ffmpeg_pass':sum(x.get('ffmpeg_strict_pass',False) for x in results.values()),'errors':[x for x in results.values() if 'error' in x]}
 (out/'summary.json').write_text(json.dumps(summary,indent=2,sort_keys=True)+'\n');print(json.dumps(summary,indent=2));return 1 if summary['errors'] or summary['base_fixed8_exact']!=196 or summary['faad_pass']!=212 or summary['ffmpeg_pass']!=212 else 0
if __name__=='__main__':raise SystemExit(main())
