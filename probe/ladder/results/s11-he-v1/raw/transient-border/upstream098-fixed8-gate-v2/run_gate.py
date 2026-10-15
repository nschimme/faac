#!/usr/bin/env python3
"""Encode/decode the upstream 098 baseline and fixed8 candidate for a 4x4 gate."""
import hashlib, json, os, subprocess, sys, wave
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
ROOT=Path('/Users/nschimme/gitprojects/faac')
BENCH=Path('/Users/nschimme/gitprojects/faac-benchmark')
OUT=Path(__file__).resolve().parent
MANIFEST=Path('/private/tmp/faac-upstream098-build-manifest.json')
CLIPS=('sandman.16b48k.wav','velvet.16b48k.wav','21-classic.441.16b48k.wav','fms.wav')
RATES=(32,48,64,96)

def sha(p):
 h=hashlib.sha256()
 with Path(p).open('rb') as f:
  for b in iter(lambda:f.read(1<<20),b''):h.update(b)
 return h.hexdigest()
def run(cmd,env=None): return subprocess.run(cmd,check=True,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,env=env)
def packet_sha(p):
 d=json.loads(run(['ffprobe','-v','error','-select_streams','a:0','-show_packets','-show_data_hash','sha256','-show_entries','packet=size,data_hash','-of','json',str(p)]).stdout)
 h=hashlib.sha256()
 for q in d['packets']:h.update(str(int(q['size'])).encode()+b':'+q['data_hash'].encode()+b'\n')
 if not d['packets']:raise RuntimeError('no AAC packets')
 return h.hexdigest(),sum(int(q['size']) for q in d['packets'])
def profile(p):
 d=json.loads(run(['ffprobe','-v','error','-select_streams','a:0','-show_entries','stream=profile','-of','json',str(p)]).stdout)
 return d['streams'][0]['profile'] if len(d.get('streams',[]))==1 else ''
def main():
 sys.path.insert(0,str(BENCH))
 from codec_bench.encoders import FAACEncoder
 from codec_bench.decoders import FDKDecoder
 import utils
 m=json.loads(MANIFEST.read_text()); variants=m['variants']; clips=m['clips']
 ffmpeg=subprocess.check_output(['which','ffmpeg'],text=True).strip()
 faad=Path('/private/tmp/faac-he-v1-decoder-build/frontend/faad')
 fdk=FDKDecoder('FDK AAC strict',str(BENCH/'bin/fdkdec'))
 for name,v in variants.items():
  if sha(v['binary'])!=v['binary_sha256'] or sha(v['library'])!=v['library_sha256']:raise RuntimeError(f'{name} build hash mismatch')
 def one(task):
  variant,clip,rate=task; v=variants[variant]; src=Path(clips[clip]['path'])
  if sha(src)!=clips[clip]['sha256']:raise RuntimeError(f'clip hash mismatch {clip}')
  tag=f'{variant}/{rate}/{Path(clip).stem}'; d=OUT/'artifacts'/variant/str(rate);d.mkdir(parents=True,exist_ok=True)
  enc=d/(Path(clip).stem+'.m4a'); raw=d/(Path(clip).stem+'.fdk-raw.wav'); dec=d/(Path(clip).stem+'.wav')
  encoder=FAACEncoder('FAAC',v['binary'],profile='he'); cmd=encoder.get_encode_cmd(str(src),str(enc),rate,2,48000)
  if '--object-type' not in cmd or cmd[cmd.index('--object-type')+1]!='he-aac-v1':raise RuntimeError('not forced HE-AAC v1')
  env=dict(os.environ)
  for k in list(env):
   if k.startswith('FAAC_SBR_'):env.pop(k)
  run(cmd,env=env)
  prof=profile(enc)
  if prof!='HE-AAC':raise RuntimeError(f'{tag}: profile={prof}')
  run(fdk.get_decode_cmd(str(enc),str(raw)),env=env)
  if not utils.wav_conv(str(raw),str(dec),rate=48000,channels=2):raise RuntimeError(f'{tag}: FDK PCM conversion failed')
  packet,bytes_n=packet_sha(enc)
  with wave.open(str(src),'rb') as w:duration=w.getnframes()/w.getframerate()
  payload=bytes_n*8/duration/1000
  faadout=d/(Path(clip).stem+'.faad.wav')
  f=run([str(faad),'--strict','--overwrite','-o',str(faadout),str(enc)],env=env); faad_ok=faadout.is_file(); faadout.unlink(missing_ok=True)
  ff=run([ffmpeg,'-v','error','-xerror','-i',str(enc),'-f','null','-'],env=env); ff_ok=not ff.stderr.strip()
  row={'variant':variant,'clip':clip,'target_kbps':rate,'profile':prof,'actual_payload_kbps':payload,'packet_payload_sha256':packet,'fdk_pcm_sha256':sha(dec),'encoded_path':str(enc),'encoded_sha256':sha(enc),'decoded_path':str(dec),'decoded_file_sha256':sha(dec),'input_path':str(src),'input_sha256':sha(src),'command':cmd,'faad_strict_pass':faad_ok,'ffmpeg_strict_pass':ff_ok,'fdk_strict_pass':True,'mos':None,'mos_source':'pending_corrected_original_wav_score'}
  return f'{variant}|{clip}|{rate}',row
 tasks=[(v,c,r) for v in variants for c in CLIPS for r in RATES]
 rows={}
 with ThreadPoolExecutor(max_workers=2) as pool:
  for key,row in pool.map(one,tasks):
   rows[key]=row
   print(f"{key} payload={row['actual_payload_kbps']:.4f} faad={row['faad_strict_pass']} ffmpeg={row['ffmpeg_strict_pass']}",flush=True)
   tmp=OUT/'validation.json.tmp';tmp.write_text(json.dumps({'schema':1,'manifest_sha256':sha(MANIFEST),'manifest':str(MANIFEST),'matrix':{'clips':list(CLIPS),'rates_kbps':list(RATES),'variants':list(variants)},'rows':rows},indent=2,sort_keys=True)+'\n');os.replace(tmp,OUT/'validation.json')
 summary={'rows':len(rows),'faad_pass':sum(r['faad_strict_pass'] for r in rows.values()),'ffmpeg_pass':sum(r['ffmpeg_strict_pass'] for r in rows.values()),'fdk_pass':sum(r['fdk_strict_pass'] for r in rows.values()),'errors':[]}
 (OUT/'summary.json').write_text(json.dumps(summary,indent=2,sort_keys=True)+'\n');print(json.dumps(summary,indent=2))
if __name__=='__main__':main()
