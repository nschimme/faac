#!/usr/bin/env python3
"""Benchmark phase-3 stereo coherence error on saved decodes, serially."""
import csv, sys
from pathlib import Path
import numpy as np
import soundfile as sf

BENCH=Path('/Users/nschimme/gitprojects/faac-benchmark')
sys.path.insert(0,str(BENCH))
from phase3_stereo import FRAME, coherence_vectorized, estimate_delay

ROOT=Path(__file__).resolve().parent/'run'
CORPUS=BENCH/'data/external/audio'
ARMS=('A-base','A-inject','B-base','B-inject')

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
    scores=list(csv.DictReader((ROOT/'scores.csv').open()))
    clips=sorted({x['clip'] for x in scores})
    files=sorted(CORPUS.glob('*.wav'))
    indices={p.name:i for i,p in enumerate(files)}
    rows=[]
    for clip in clips:
        ref,sr=sf.read(CORPUS/clip,dtype='float64',always_2d=True)
        if sr!=48000 or ref.shape[1]!=2:raise RuntimeError(f'bad source {clip}')
        for rate in (40,48,64):
            d=ROOT/f'{indices[clip]:02d}_{Path(clip).stem}'/str(rate)
            for arm in ARMS:
                for decoder in ('ff','fdk'):
                    wav=d/arm/f'{decoder}.wav'
                    deg,dr=sf.read(wav,dtype='float64',always_2d=True)
                    if dr!=48000 or deg.shape[1]!=2:raise RuntimeError(f'bad decode {wav}')
                    rows.append((clip,rate,arm,decoder,error(ref,deg)))
        print('coherence',clip,flush=True)
    with (ROOT/'coherence.csv').open('w',newline='') as f:
        w=csv.writer(f);w.writerow(('clip','rate','arm','decoder','error'));w.writerows(rows)
if __name__=='__main__':main()
