#!/usr/bin/env python3
import csv,sys
from pathlib import Path
import numpy as np,soundfile as sf
BENCH=Path('/Users/nschimme/gitprojects/faac-benchmark')
sys.path.insert(0,str(BENCH))
from phase3_stereo import FRAME,coherence_vectorized,estimate_delay
from xover_run import ROOT,CORPUS,CONFIG
ARMS=('N15','N13','G13','S13','F')
def error(ref,deg):
 if np.array_equal(ref[:,0],ref[:,1]):return None
 lag=estimate_delay(ref[:,0],deg[:,0])
 if lag>0:deg=deg[lag:]
 elif lag<0:ref=ref[-lag:]
 n=min(len(ref),len(deg));ref=ref[:n];deg=deg[:n]
 if n<FRAME:return None
 a=coherence_vectorized(ref[:,0],ref[:,1],FRAME)
 b=coherence_vectorized(deg[:,0],deg[:,1],FRAME)
 return float(np.mean(np.abs(a-b)))
def main():
 clips=sorted(CORPUS.glob('*.wav'))
 rows=[]
 for ci,p in enumerate(clips):
  ref,sr=sf.read(p,dtype='float64',always_2d=True)
  if sr!=48000 or ref.shape[1]!=2:raise RuntimeError('HARNESS ERROR: source '+str(p))
  for rate in CONFIG:
   d=ROOT/f'{ci:02d}_{p.stem}'/str(rate)
   if not (d/'done').exists():raise RuntimeError('HARNESS ERROR: incomplete '+str(d))
   for arm in ARMS:
    for dec in ('ff','fdk'):
     wav=(d/f'S13-{dec}.wav') if arm=='S13' else (d/arm/f'{dec}.wav')
     x,xsr=sf.read(wav,dtype='float64',always_2d=True)
     if xsr!=48000 or x.shape[1]!=2:raise RuntimeError('HARNESS ERROR: decode '+str(wav))
     rows.append((p.name,rate,arm,dec,error(ref,x)))
  print('coherence',ci+1,p.name,flush=True)
 with (ROOT/'coherence.csv').open('w',newline='') as f:
  w=csv.writer(f);w.writerow(('clip','rate','arm','decoder','error'));w.writerows(rows)
if __name__=='__main__':main()
