#!/usr/bin/env python3
"""LC identity and strict HE-CBR mono/stereo/silence/partial-flush checks."""
import argparse,hashlib,json,math,os,struct,subprocess,sys,tempfile,wave
from pathlib import Path

BENCH=Path('/Users/nschimme/gitprojects/faac-benchmark')
FAAD=Path('/private/tmp/faac-he-v1-decoder-build/frontend/faad')
FFMPEG=Path('/opt/homebrew/bin/ffmpeg')
BASE=Path('/private/tmp/faac-pr-baseline/build-pub/frontend/faac')
OUT=Path('/Users/nschimme/gitprojects/faac/.cache/he-v1-feature-study/transient-border/upstream-fixed8-validation/smoke')
def run(cmd,env=None):return subprocess.run(cmd,check=True,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,env=env)
def sha(p):
 h=hashlib.sha256();
 with Path(p).open('rb') as f:
  for b in iter(lambda:f.read(1<<20),b''):h.update(b)
 return h.hexdigest()
def prof(p):
 q=json.loads(run(['ffprobe','-v','error','-select_streams','a:0','-show_entries','stream=profile','-of','json',str(p)]).stdout)['streams']
 if len(q)!=1:raise RuntimeError(f'expected one stream {p}')
 return q[0]['profile']
def packet(p):
 q=json.loads(run(['ffprobe','-v','error','-select_streams','a:0','-show_packets','-show_data_hash','sha256','-show_entries','packet=size,data_hash','-of','json',str(p)]).stdout)['packets']
 h=hashlib.sha256()
 for x in q:h.update(str(x['size']).encode()+b':'+x['data_hash'].encode()+b'\n')
 return h.hexdigest()
def write_wav(path,channels,samples,kind='tone'):
 path.parent.mkdir(parents=True,exist_ok=True)
 with wave.open(str(path),'wb') as w:
  w.setnchannels(channels);w.setsampwidth(2);w.setframerate(48000)
  out=bytearray()
  for i in range(samples):
   value=0 if kind=='silence' else int(5000*math.sin(2*math.pi*997*i/48000))
   for _ in range(channels):out.extend(struct.pack('<h',value))
  w.writeframes(out)
def safe_env(decoder=None):
 e=dict(os.environ)
 for k in list(e):
  if k.startswith('FAAC_SBR_'):e.pop(k)
 if decoder:e.update(decoder.get_run_env())
 return e
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--binary',type=Path,required=True);ap.add_argument('--label',required=True);args=ap.parse_args()
 sys.path.insert(0,str(BENCH));from codec_bench.encoders import FAACEncoder;from codec_bench.decoders import FDKDecoder;import utils
 out=OUT/args.label;out.mkdir(parents=True,exist_ok=True);dec=FDKDecoder('FDK AAC strict',str(BENCH/'bin/fdkdec'));rows=[]
 # AAC-LC is untouched by this SBR-only change; compare packet identity to clean 01e7.
 src=BENCH/'data/external/audio/fms.wav'
 with tempfile.TemporaryDirectory(prefix='fixed8-lc-') as td:
  td=Path(td);paths=[]
  for name,binary in [('baseline',BASE),('candidate',args.binary)]:
   enc=td/f'{name}.m4a';encoder=FAACEncoder(name,str(binary),profile='lc');cmd=encoder.get_encode_cmd(str(src),str(enc),128,2,48000,rate_control='abr')
   run(cmd,env=safe_env());paths.append(enc)
  rows.append({'test':'LC ABR packet identity','profile_baseline':prof(paths[0]),'profile_candidate':prof(paths[1]),'packet_sha_baseline':packet(paths[0]),'packet_sha_candidate':packet(paths[1]),'packet_identity':packet(paths[0])==packet(paths[1])})
 # Synthetic tests exercise mono/stereo, silence, CBR range endpoints, and a tail not divisible by AAC frame size.
 cases=[('mono_cbr32_partial',1,12345,'tone',32),('stereo_cbr32_partial',2,12345,'tone',32),('mono_cbr96_boundary',1,48000,'tone',96),('stereo_cbr96_boundary',2,48000,'tone',96),('mono_silence_cbr32',1,8191,'silence',32),('stereo_silence_cbr32',2,8191,'silence',32),('mono_cbr64_partial',1,5003,'tone',64),('stereo_cbr48_partial',2,5003,'tone',48)]
 for name,ch,n,kind,rate in cases:
  source=out/(name+'.input.wav');write_wav(source,ch,n,kind)
  encoded=out/(name+'.m4a');encoder=FAACEncoder(args.label,str(args.binary),profile='he');cmd=encoder.get_encode_cmd(str(source),str(encoded),rate,ch,48000,rate_control='cbr')
  if '--object-type' not in cmd or cmd[cmd.index('--object-type')+1]!='he-aac-v1':raise RuntimeError(f'not HE-v1 forced: {cmd}')
  run(cmd,env=safe_env());profile=prof(encoded)
  if profile!='HE-AAC':raise RuntimeError(f'{name}: profile {profile}')
  fdkwav=out/(name+'.fdk.wav');run(dec.get_decode_cmd(str(encoded),str(fdkwav)),env=safe_env(dec))
  pcm=out/(name+'.pcm.wav')
  if not utils.wav_conv(str(fdkwav),str(pcm),rate=48000,channels=ch):raise RuntimeError(f'{name}: FDK convert failed')
  faadwav=out/(name+'.faad.wav');faad=run([str(FAAD),'--strict','--overwrite','-o',str(faadwav),str(encoded)])
  ff=run([str(FFMPEG),'-v','error','-xerror','-i',str(encoded),'-f','null','-'])
  rows.append({'test':name,'channels':ch,'samples':n,'partial_frame':n%1024!=0,'target_kbps_total':rate,'profile':profile,'payload_sha256':packet(encoded),'decoded_pcm_sha256':sha(pcm),'fdk_pass':True,'faad_pass':faad.returncode==0 and faadwav.is_file(),'ffmpeg_pass':ff.returncode==0 and not ff.stderr.strip(),'encoded_path':str(encoded),'decoded_path':str(pcm),'command':cmd})
  if rows[-1]['faad_pass'] is False or rows[-1]['ffmpeg_pass'] is False:raise RuntimeError(f'{name}: decoder acceptance failed')
 result={'schema':1,'label':args.label,'binary':str(args.binary),'binary_sha256':sha(args.binary),'tests':rows,'passed':sum((r.get('packet_identity',True) and r.get('fdk_pass',True) and r.get('faad_pass',True) and r.get('ffmpeg_pass',True)) for r in rows),'total':len(rows)}
 (out/'smoke.json').write_text(json.dumps(result,indent=2,sort_keys=True)+'\n');print(json.dumps(result,indent=2));return 0 if result['passed']==result['total'] else 1
if __name__=='__main__':raise SystemExit(main())
