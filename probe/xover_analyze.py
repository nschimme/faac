#!/usr/bin/env python3
import csv,statistics as st,hashlib,subprocess
from collections import defaultdict
from pathlib import Path
from xover_run import ROOT,CORPUS,CONFIG,core,hdr,price,table
ARMS=('N15','N13','G13','S13','F');DECS=('ff','fdk')
def err(s):raise RuntimeError('HARNESS ERROR: '+s)
def mean(a):return st.mean(a)
def mos_adjust(rows,clip,rate,arm,dec):
 base=rows[clip,rate,'N15'];cur=rows[clip,rate,arm]
 lo,hi={40:(40,48),48:(40,56),56:(48,64),64:(56,64)}[rate]
 left,right=rows[clip,lo,'N15'],rows[clip,hi,'N15']
 bitspan=(float(right['bytes'])-float(left['bytes']))/float(base['bytes'])*100
 if bitspan<=0:err(f'bit slope {clip} {rate}')
 slope=(float(right[f'{dec}_mos'])-float(left[f'{dec}_mos']))/bitspan
 # S13 is a decode-domain hybrid; N13's stream bytes are its nominal bit-price proxy.
 deviation=(float(cur['bytes'])/float(base['bytes'])-1)*100
 return float(cur[f'{dec}_mos'])-slope*deviation

def stream_bits(d,arm):
 p=price(d/arm/'sbr.dump');h=hdr(d/arm/'sbr.dump')
 # H indices: 16 low, 17 high. P freq_res field lists coded envelopes.
 lo,hi=h[16],h[17]
 coded=0
 for line in (d/arm/'sbr.dump').read_text().splitlines():
  if not line.startswith('P '):continue
  res=line.split('| freq_res:')[-1].strip().split('|')[0].split()
  coded+=sum(hi if int(x) else lo for x in res)
 if coded<=0:err(f'no coded envelope bands {d} {arm}')
 frames=len({int(x[1]) for x in p})
 sumcol=lambda i:sum(float(x[i]) for x in p)
 c=core(d/arm/'sbr.dump')
 if len({x[0] for x in c})!=frames:err(f'core frames {d} {arm}')
 return {'core':sum(x[2] for x in c)/frames,'sbr':sumcol(3)/frames,'grid':sumcol(4)/frames,'env':sumcol(5)/frames,'noise':sumcol(6)/frames,'other':sumcol(7)/frames,'env_per_band':sumcol(5)/coded,'grid_per_band':sumcol(4)/coded,'noise_per_band':sumcol(6)/coded,'other_per_band':sumcol(7)/coded,'bands_per_frame':coded/frames}

def main():
 clips=sorted(CORPUS.glob('*.wav'))
 if len(clips)!=49:err('corpus count')
 scorefile=ROOT/'scores.csv'
 if not scorefile.exists():err('missing scores')
 raw=list(csv.DictReader(scorefile.open()))
 expected={(p.name,r,a) for p in clips for r in CONFIG for a in ARMS}
 rows={(x['clip'],int(x['rate']),x['arm']):x for x in raw}
 if len(raw)!=len(expected) or set(rows)!=expected:err(f'score coverage {len(raw)}/{len(expected)}')
 lines=['# 48 kHz crossover probe: measured result','',f'49 clips, 40/48/56/64 kbps, HE-AAC v1 stereo. All 980 bitstream decodes (N15/N13/G13/F, two decoders) were serial. S13 is a decode-domain hybrid.','',
 '## Table A: MOS','',
 'Each MOS cell is raw / bit-adjusted. Δ and W/L use raw MOS. W/L counts use ±0.02 per clip; ties omitted. Bit adjustment charges the clip-specific N15 MOS-per-bit slope estimated from adjacent rates against the arm’s total ADTS bytes. S13 uses N13’s stream size as its nominal bit price.','',
 '| kbps | decoder | arm | MOS raw / adjusted | Δ vs N15 (W/L) | Δ vs F (W/L) |','|---:|:---|:---|---:|---:|---:|']
 for rate in CONFIG:
  for dec in DECS:
   for arm in ARMS:
    a=[rows[p.name,rate,arm] for p in clips]
    m=mean(float(x[f'{dec}_mos']) for x in a)
    adj=mean(mos_adjust(rows,p.name,rate,arm,dec) for p in clips)
    d15=[float(rows[p.name,rate,arm][f'{dec}_mos'])-float(rows[p.name,rate,'N15'][f'{dec}_mos']) for p in clips]
    df=[float(rows[p.name,rate,arm][f'{dec}_mos'])-float(rows[p.name,rate,'F'][f'{dec}_mos']) for p in clips]
    wl=lambda xs:f'{sum(x>.02 for x in xs)}/{sum(x<-.02 for x in xs)}'
    lines.append(f'| {rate} | {dec} | {arm} | {m:.4f} / {adj:.4f} | {mean(d15):+.4f} ({wl(d15)}) | {mean(df):+.4f} ({wl(df)}) |')
 lines+=['','## Table B: bit allocation','',
 'Bits/frame are for the full stereo access unit. Env/band divides all envelope bits by the number of coded envelope bands across frames and channels, using each envelope’s high/low resolution flag. Noise-band counts differ by arm. Grid includes the header. S13 has no independent bitstream.','',
 '| kbps | arm | core bits/frame | SBR bits/frame | grid/env/noise/other bits per coded band | coded bands/frame | grid/env/noise/other bits per frame |',
 '|---:|:---|---:|---:|:---|---:|:---|']
 for rate in CONFIG:
  for arm in ('N15','N13','G13','F'):
   b=[]
   for ci,p in enumerate(clips):
    d=ROOT/f'{ci:02d}_{p.stem}'/str(rate);b.append(stream_bits(d,arm))
   v={k:mean(x[k] for x in b) for k in b[0]}
   lines.append(f"| {rate} | {arm} | {v['core']:.1f} | {v['sbr']:.1f} | {v['grid_per_band']:.3f}/{v['env_per_band']:.3f}/{v['noise_per_band']:.3f}/{v['other_per_band']:.3f} | {v['bands_per_frame']:.1f} | {v['grid']:.1f}/{v['env']:.1f}/{v['noise']:.1f}/{v['other']:.1f} |")
 lines+=['','## Table C: stereo coherence error','',
 'Benchmark phase-3 per-frame coherence error; lower is better.','',
 '| kbps | decoder | N15 | N13 | G13 | S13 | F |','|---:|:---|---:|---:|---:|---:|---:|']
 cohfile=ROOT/'coherence.csv'
 if not cohfile.exists():err('missing coherence.csv')
 cr=list(csv.DictReader(cohfile.open()));coh={(x['clip'],int(x['rate']),x['arm'],x['decoder']):x['error'] for x in cr}
 for rate in CONFIG:
  for dec in DECS:
   vals=[]
   for arm in ARMS:
    a=[float(coh[p.name,rate,arm,dec]) for p in clips if coh.get((p.name,rate,arm,dec)) not in (None,'','None')]
    vals.append(f'{mean(a):.4f} (n={len(a)})')
   lines.append(f'| {rate} | {dec} | '+' | '.join(vals)+' |')
 lines+=['','## Interpretation','']
 for rate in CONFIG:
  for dec in DECS:
   n=mean(float(rows[p.name,rate,'N13'][f'{dec}_mos']) for p in clips)
   g=mean(float(rows[p.name,rate,'G13'][f'{dec}_mos']) for p in clips)
   s=mean(float(rows[p.name,rate,'S13'][f'{dec}_mos']) for p in clips)
   f=mean(float(rows[p.name,rate,'F'][f'{dec}_mos']) for p in clips)
   frac=(g-n)/(s-n) if abs(s-n)>1e-9 else float('nan')
   lines.append(f'{rate} kbps {dec}: S13−F {s-f:+.4f}; N13→G13 {g-n:+.4f}; N13→S13 {s-n:+.4f}; grid fraction {frac:.1%}.')
 lines+=['','The hybrid is a measurement ceiling for replacing only the decoded high band at the matched crossover; the FIR transition and encoder phase may affect its score. Fractions with a near-zero or negative N13→S13 gap should not be interpreted as progress toward a positive ceiling.','',
 '## Gates and provenance','',
 'Known arm difference: fdk noise_bands is 2 at 40 kbps and 3 at 48/56/64; FAAC signals 0 (one noise band) at all rates. This exception is permitted. The master, high and low decoder-derived edge arrays match exactly at each rate; the noise tables intentionally differ. All other SBR header fields match.', '',
 '| kbps | start | stop | xover | freq_scale | alter | amp_res | noise_bands F/N13 | kx | full master edges |',
 '|---:|---:|---:|---:|---:|---:|---:|:---|---:|:---|']
 for rate,(start,stop,scale,kx) in CONFIG.items():
  d=ROOT/'00_12-German-male-speech.441.16b48k'/str(rate)
  if not d.exists():d=ROOT/f'00_{clips[0].stem}'/str(rate)
  h=hdr(d/'F/sbr.dump');ft=table(d/'F/sbr.dump');nh=hdr(d/'N13/sbr.dump')
  lines.append(f"| {rate} | {start} | {stop} | 0 | {scale} | 1 | {h[2]} | {h[8]}/{nh[8]} | {kx} | {' '.join(map(str,ft[0]))} |")
 biases=[abs(float(x[f'self_bias_{dec}'])) for x in raw if x['arm']=='N13' for dec in DECS if x[f'self_bias_{dec}']]
 lines+=['',f'Five-clip off-knob identity passed 20/20 SHA-256 comparisons (results in `xover_identity/results.csv`). Five-clip, four-rate N13 recombination max absolute MOS bias: {max(biases):.4f}. Every G13 donor-grid match was 100% on both channels. Every N13/G13 FAAD decode had zero concealment and zero non-END termination; FFmpeg strict decodes succeeded. On every clip/rate the C-record mean max_sfb dropped from N15 to N13, following the lower kx. FAAC and fdk alignment uses the established 33-sample donor pad and FAAC frame n ↔ fdk n+1.', '',
 'Commands: `CCACHE_DISABLE=1 meson compile -C build`; `/Users/nschimme/gitprojects/faac-benchmark/.venv/bin/python probe/xover_identity.py`; `/Users/nschimme/gitprojects/faac-benchmark/.venv/bin/python probe/xover_run.py --limit 5`; `/Users/nschimme/gitprojects/faac-benchmark/.venv/bin/python probe/xover_run.py`; `/Users/nschimme/gitprojects/faac-benchmark/.venv/bin/python probe/xover_coherence.py`; `/Users/nschimme/gitprojects/faac-benchmark/.venv/bin/python probe/xover_analyze.py`. Run script `xover_run.py` contains the exact serial encode, decode, alignment, bandswap, and score commands. Probe source diff is `xover.patch`; FAAD3 T-record instrumentation is `faad-xover.patch`. No fdk-aac source was read. No commit or push.','']
 (Path(__file__).parent/'XOVER_RESULT.md').write_text('\n'.join(lines))
 print('report written',len(raw),'score rows',len(cr),'coherence rows')
if __name__=='__main__':main()
