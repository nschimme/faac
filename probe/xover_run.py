#!/usr/bin/env python3
"""Serial crossover probe. Fails closed on identity, table, decode, grid and hybrid gates."""
import csv,hashlib,os,re,subprocess,sys,argparse
os.environ.setdefault('NUMBA_DISABLE_JIT','1')
from pathlib import Path
import numpy as np,soundfile as sf
sys.path.insert(0,str(Path(__file__).parent))
from grid_score import grid_match
from price_run import check_writer,price_dump,ff_decode,profile,scorer,run
BENCH=Path('/Users/nschimme/gitprojects/faac-benchmark')
sys.path.insert(0,str(BENCH/'scripts'))
from bandswap import align_to_ref,fir_split
ROOT=Path(__file__).parent/'xover_run';ROOT.mkdir(exist_ok=True)
CORPUS=BENCH/'data/external/audio'
FAAC=Path(__file__).parents[1]/'build/frontend/faac'
FDKDEC=BENCH/'bin/fdkdec'
FAAD=Path('/private/tmp/claude-501/faac-work/faad3/bs/frontend/faad')
CONFIG={40:(10,9,2,18),48:(12,9,2,22),56:(13,10,1,24),64:(14,12,1,27)}
FIELDS=['clip','rate','arm','bytes','ff_mos','fdk_mos','core_bits','sbr_bits','grid0','grid1','self_bias_ff','self_bias_fdk']
def fail(msg):raise RuntimeError('HARNESS ERROR: '+msg)
def env_xover(rate):
 start,stop,scale,_=CONFIG[rate]
 return dict(FAAC_SBR_START=str(start),FAAC_SBR_STOP=str(stop),FAAC_SBR_FREQ_SCALE=str(scale),FAAC_SBR_ALTER='1')
def faad_decode(stream,ad):
 dump=ad/'sbr.dump';dump.unlink(missing_ok=True)
 p=run([FAAD,'--strict','-q','-b','32f','-o',ad/'faad.wav',stream],{'FAAD_DUMP':str(dump),'FAAD_DUMPTAB':'1'})
 if 'Error concealment   : 0 frames' not in p.stderr or 'non-END termination: 0' not in p.stderr:fail(f'FAAD concealment {stream}: {p.stderr[-1200:]}')
 return dump

def hdr(path):
 line=next((x for x in path.read_text().splitlines() if x.startswith('H ')),None)
 if not line:fail(f'no header {path}')
 return list(map(int,line.split()[1:]))
def table(path):
 line=next((x for x in path.read_text().splitlines() if x.startswith('T ')),None)
 if not line:fail(f'no T record {path}')
 return [tuple(map(int,part.split()[3:] if i==0 else part.split()[1:])) for i,part in enumerate(line.split(' | '))]
def core(path):
 vals=[list(map(int,x.split(' |')[0].split()[1:])) for x in path.read_text().splitlines() if x.startswith('C ')]
 if not vals:fail(f'no core C {path}')
 return vals
def price(path):
 vals=[]
 for line in path.read_text().splitlines():
  if line.startswith('P '):
   w=line.split(' |')[0].split();vals.append(w)
 if not vals:fail(f'no P records {path}')
 for fr in set(int(x[1]) for x in vals):
  pair=[x for x in vals if int(x[1])==fr]
  if len(pair)!=2 or int(pair[0][14])>7 or int(pair[1][14])>7 or abs(sum(float(x[3]) for x in pair)-int(pair[0][13]))>0.01:fail(f'P boundary {path} {fr}')
 return vals
def hybrid(ref,low_wav,high_wav,cutoff,out):
 x,sr=sf.read(ref,dtype='float32',always_2d=True)
 lo,sl=sf.read(low_wav,dtype='float32',always_2d=True)
 hi,sh=sf.read(high_wav,dtype='float32',always_2d=True)
 if sr!=48000 or sl!=sr or sh!=sr or x.shape[1]!=2 or lo.shape[1]!=2 or hi.shape[1]!=2:fail(f'hybrid format {out}')
 lo,laglo=align_to_ref(x,lo);hi,laghi=align_to_ref(x,hi)
 if abs(laglo)>8192 or abs(laghi)>8192:fail(f'hybrid lag {out}: {laglo},{laghi}')
 a,b=fir_split(lo,48000,cutoff);c,d=fir_split(hi,48000,cutoff)
 sf.write(out,a+d,48000,subtype='FLOAT')
 return laglo,laghi

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--limit',type=int,default=49);args=ap.parse_args()
 clips=sorted(CORPUS.glob('*.wav'))
 if len(clips)!=49:fail(f'corpus length {len(clips)}')
 for ci,ref in enumerate(clips[:args.limit]):
  x,sr=sf.read(ref,dtype='float32',always_2d=True)
  if sr!=48000 or x.shape[1]!=2:fail(f'source {ref}')
  cd=ROOT/f'{ci:02d}_{ref.stem}';cd.mkdir(exist_ok=True)
  padded=cd/'padded.wav'
  if not padded.exists():sf.write(padded,np.vstack((np.zeros((33,2),np.float32),x)),48000,subtype='FLOAT')
  for rate,(_,_,_,kx) in CONFIG.items():
   d=cd/str(rate);d.mkdir(exist_ok=True)
   marker=d/'done'
   if marker.exists():continue
   f=d/'F';f.mkdir(exist_ok=True);fs=f/'stream.aac'
   run(['fdkaac','-p','5','-b',rate*1000,'-f','2','-o',fs,padded]);profile(fs)
   fd=faad_decode(fs,f);ff_decode(fs,f/'ff.wav');run([FDKDEC,fs,f/'fdk.wav']);price(fd)
   donor=hdr(fd);ft=table(fd)
   expected=CONFIG[rate]
   if (donor[3],donor[4],donor[6],donor[14])!=(expected[0],expected[1],expected[2],kx):fail(f'donor H changed {rate}: {donor}')
   rows=[];data={}
   for arm in ('N15','N13','G13'):
    ad=d/arm;ad.mkdir(exist_ok=True);stream=ad/'stream.aac';env={'FAAC_PRICE_DUMP':str(ad/'writer.dump')}
    if arm!='N15':env.update(env_xover(rate))
    if arm=='G13':env.update(FAAC_SBR_INJECT=str(fd.resolve()),FAAC_SBR_INJECT_FIELDS='grid',FAAC_SBR_INJECT_OFFSET='1')
    (ad/'writer.dump').unlink(missing_ok=True)
    run([FAAC,'--overwrite','--object-type','he-aac-v1','-b',rate,'-o',stream,ref],env)
    profile(stream);dump=faad_decode(stream,ad);check_writer(ad);price(dump)
    ff_decode(stream,ad/'ff.wav');run([FDKDEC,stream,ad/'fdk.wav'])
    h=hdr(dump)
    if arm!='N15':
     for i in (2,3,4,5,6,7,14,15,16,17):
      if h[i]!=donor[i]:fail(f'header {arm} {rate} field {i}: {h[i]} vs {donor[i]}')
     if h[8]!=0 or donor[8] not in (2,3):fail(f'noise exception drift {rate}: {h[8]} {donor[8]}')
     if table(dump)!=ft:fail(f'full master/high/low table mismatch {ref.name} {rate} {arm}')
    g=grid_match(fd,dump) if arm=='G13' else [float('nan')]*2
    if arm=='G13' and min(g)<99.99:fail(f'grid {ref.name} {rate}: {g}')
    data[arm]=(ad,stream,dump)
    w=core(ad/'writer.dump');rows.append(dict(clip=ref.name,rate=rate,arm=arm,bytes=stream.stat().st_size,ff_mos=scorer(ref,ad/'ff.wav'),fdk_mos=scorer(ref,ad/'fdk.wav'),core_bits=sum(v[3] for v in w)/len(w),sbr_bits=sum(v[2] for v in w)/len(w),grid0=g[0],grid1=g[1],self_bias_ff='',self_bias_fdk=''))
   c15=core(data['N15'][2]);c13=core(data['N13'][2])
   # C: frame,ch,bits,window_sequence,max_sfb,...; compare same clip after lead-in.
   a=[v[4] for v in c15 if v[0]>=3];b=[v[4] for v in c13 if v[0]>=3]
   if not a or not b or sum(b)/len(b)>=sum(a)/len(a):fail(f'core max_sfb did not move {ref.name} {rate}: {sum(a)/len(a)}, {sum(b)/len(b)}')
   # Recombine N13 with itself through bandswap's alignment and FIR paths.
   for decoder in (('ff','fdk') if ci<5 else ()):
    selfwav=d/f'self-{decoder}.wav';hybrid(ref,data['N13'][0]/f'{decoder}.wav',data['N13'][0]/f'{decoder}.wav',kx*375,selfwav)
    selfscore=scorer(ref,selfwav)
    base=next(float(v[f'{decoder}_mos']) for v in rows if v['arm']=='N13')
    bias=selfscore-base
    if abs(bias)>0.02:fail(f'bandswap recombine bias {ref.name} {rate} {decoder}: {bias}')
    next(v for v in rows if v['arm']=='N13')[f'self_bias_{decoder}']=bias
   for decoder in ('ff','fdk'):
    hybrid(ref,data['N13'][0]/f'{decoder}.wav',f/f'{decoder}.wav',kx*375,d/f'S13-{decoder}.wav')
   rows.append(dict(clip=ref.name,rate=rate,arm='S13',bytes=data['N13'][1].stat().st_size,ff_mos=scorer(ref,d/'S13-ff.wav'),fdk_mos=scorer(ref,d/'S13-fdk.wav'),core_bits='',sbr_bits='',grid0='',grid1='',self_bias_ff='',self_bias_fdk=''))
   rows.append(dict(clip=ref.name,rate=rate,arm='F',bytes=fs.stat().st_size,ff_mos=scorer(ref,f/'ff.wav'),fdk_mos=scorer(ref,f/'fdk.wav'),core_bits=sum(v[2] for v in core(fd))/len(core(fd)),sbr_bits=sum(float(v[3]) for v in price(fd))/len(core(fd)),grid0='',grid1='',self_bias_ff='',self_bias_fdk=''))
   with (ROOT/'scores.csv').open('a',newline='') as h:
    writer=csv.DictWriter(h,fieldnames=FIELDS)
    if h.tell()==0:writer.writeheader()
    writer.writerows(rows)
   marker.write_text('ok\n');print(f'{ci+1}/49 {rate} PASS '+', '.join(f"{v['arm']}={float(v['ff_mos']):.3f}" for v in rows),flush=True)
if __name__=='__main__':
 try:main()
 except Exception as ex:print(str(ex),file=sys.stderr,flush=True);sys.exit(2)
