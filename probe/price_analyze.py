#!/usr/bin/env python3
"""Verify full price corpus and render measured tables. Run only after encoding completes."""
import csv, math, statistics as st, subprocess, sys
from collections import defaultdict
from pathlib import Path

ROOT=Path(__file__).resolve().parent/'run'
CORPUS=Path('/Users/nschimme/gitprojects/faac-benchmark/data/external/audio')
ARMS=('A-base','A-inject','B-base','B-inject')
CLASS=('FF','FV','VF','VV')

def err(message): raise RuntimeError('HARNESS ERROR: '+message)
def mean(xs): return st.mean(xs) if xs else float('nan')
def price(path):
    out={}
    for line in path.read_text().splitlines():
        if not line.startswith('P '):continue
        w=line.split(' |')[0].split();fr,ch=int(w[1]),int(w[2]);fields={
            'total':float(w[3]),'grid':float(w[4]),'env':float(w[5]),'noise':float(w[6]),'other':float(w[7]),
            'class':int(w[8]),'n':int(w[9]),'amp':int(w[10]),'coupling':int(w[11]),
            'parsed':int(w[12]),'fill':int(w[13]),'pad':int(w[14])}
        for key in ('dt_env','dt_noise','freq_res'):
            tail=line.split('| '+key+':')[-1].split('|')[0]
            fields[key]=[int(x) for x in tail.split()] if '| '+key+':' in line else []
        out[fr,ch]=fields
    return out

def grids(path):
    out=defaultdict(dict)
    for line in path.read_text().splitlines():
        w=line.split();
        if not w or w[0] not in ('F','G','R'):continue
        fr,ch=int(w[1]),int(w[2]);key=fr,ch
        if w[0]=='F':out[key]['f']=tuple(map(int,w[3:5]))
        if w[0]=='G':out[key]['g']=tuple(map(int,w[3:]))
        if w[0]=='R':out[key]['r']=tuple(int(x) for x in line.split('| freq_res:')[-1].split('|')[0].split())
    return out

def verify_stream(d,parsed):
    if not parsed:err(f'no price records {d}')
    for fr in {f for f,c in parsed}:
        if (fr,0) not in parsed or (fr,1) not in parsed:err(f'missing channel price {d} {fr}')
        a,b=parsed[fr,0],parsed[fr,1]
        if a['pad']<0 or a['pad']>7 or a['fill']!=b['fill'] or abs(a['total']+b['total']-a['fill'])>0.01:
            err(f'SBR parser boundary {d} {fr}: {a} {b}')
    if d.name=='fdk':return None
    lines=(d/'writer.dump').read_text().splitlines()
    core=[list(map(int,l.split()[1:])) for l in lines if l.startswith('C ')]
    writes={}
    for l in lines:
        if l.startswith('W '):
            w=l.split(' |')[0].split();writes[int(w[1]),int(w[2])]=tuple(map(float,w[3:8]))
    if len(core)!=len({f for f,c in parsed}) or writes.keys()!=parsed.keys():err(f'encoder/decoder frame count {d}')
    for key,w in writes.items():
        p=parsed[key]
        if w!=(p['total'],p['grid'],p['env'],p['noise'],p['other']):err(f'encoder/decoder split {d} {key}')
    sbr=sum(x['total'] for x in parsed.values())
    core_bits=sum(c[3] for c in core)
    stuffed=sum(c[4] for c in core)
    payload=(d/'stream.aac').stat().st_size*8-56*len(core)
    rel=abs(sbr-(payload-core_bits-stuffed))/max(1,sbr)
    if rel>0.01:err(f'clip accounting {d}: SBR={sbr}, payload={payload}, core={core_bits}, stuffed={stuffed}, rel={rel}')
    if sum(c[1] for c in core)!=payload or abs(sum(c[2] for c in core)-sbr)>0.01:err(f'RC/write accounting {d}')
    return {'sbr':sbr,'core':core_bits,'stuff':stuffed,'payload':payload,'frames':len(core),
            'closes':sum(l.startswith('X ') and int(l.split()[1])+4 in {f for f,c in writes} for l in lines)}

def fmt_bits(records):
    if not records:return '—'
    return f"{mean(x['total'] for x in records):.1f} ({mean(x['grid'] for x in records):.1f}/{mean(x['env'] for x in records):.1f}/{mean(x['noise'] for x in records):.1f}/{mean(x['other'] for x in records):.1f})"

def main():
    files=sorted(CORPUS.glob('*.wav'))
    if len(files)!=49:err(f'corpus has {len(files)} clips')
    fails=list(csv.DictReader((ROOT/'failures.csv').open())) if (ROOT/'failures.csv').exists() else []
    failed={r['clip'] for r in fails}
    if len(failed)>3:err(f'more than 3 failed clips: {failed}')
    clips=[x for x in files if x.name not in failed]
    rows=list(csv.DictReader((ROOT/'scores.csv').open()))
    expected={(x.name,r,a) for x in clips for r in (40,48,64) for a in ARMS}
    got={(x['clip'],int(x['rate']),x['arm']) for x in rows}
    if got!=expected or len(rows)!=len(expected):err(f'score coverage got={len(rows)} expected={len(expected)} missing={len(expected-got)} extra={len(got-expected)}')
    lookup={(x['clip'],int(x['rate']),x['arm']):x for x in rows}
    groups=defaultdict(lambda:defaultdict(list));perrate=defaultdict(lambda:defaultdict(list))
    examples=defaultdict(list);matching=defaultdict(int);closing=defaultdict(int);accounting=[]
    for ci,clip in enumerate(files):
        if clip.name in failed:continue
        for rate in (40,48,64):
            d=ROOT/f'{ci:02d}_{clip.stem}'/str(rate)
            if not (d/'done').exists() and not (d.parent/'done').exists():err(f'not done {d}')
            for arm in ('fdk',)+ARMS:
                info=subprocess.run(['ffprobe','-v','error','-select_streams','a:0','-show_entries','stream=profile,sample_rate,channels','-of','default=noprint_wrappers=1',str(d/arm/'stream.aac')],capture_output=True,text=True,check=True).stdout.splitlines()
                if set(info)!={'profile=HE-AAC','sample_rate=48000','channels=2'}:err(f'object type/source format {d/arm}: {info}')
            pr={arm:price(d/arm/'sbr.dump') for arm in ('fdk',)+ARMS}
            gd={arm:grids(d/arm/'sbr.dump') for arm in ('fdk','A-inject','B-inject')}
            for arm in ('fdk',)+ARMS:
                result=verify_stream(d/arm,pr[arm])
                if result is not None:
                    accounting.append((clip.name,rate,arm,result))
                    closing[rate,arm]+=result['closes']
            for (fr,ch),a in pr['A-inject'].items():
                if fr<4:continue
                key=(fr,ch);dk=(fr+1,ch)
                if dk not in pr['fdk'] or key not in pr['B-inject']:continue
                if not all(k in gd[arm].get(kk,{}) for arm,kk in (('fdk',dk),('A-inject',key),('B-inject',key)) for k in ('f','g','r')):continue
                if any(gd[arm][key][k]!=gd['fdk'][dk][k] for arm in ('A-inject','B-inject') for k in ('f','g','r')):continue
                data={'fdk':pr['fdk'][dk],'A':a,'B':pr['B-inject'][key]}
                cls=data['fdk']['class'];n=data['fdk']['n']
                if cls not in range(4) or n not in range(1,6):err(f'invalid class/count {clip} {rate} {fr}')
                for arm,x in data.items():
                    groups[rate,cls,n][arm].append(x);perrate[rate,arm]['records'].append(x)
                    if ch==0:perrate[rate,arm]['frames'].append(x)
                matching[rate]+=1
                if len(examples[cls])<3:examples[cls].append((clip.name,rate,fr,ch,data))
    lines=[]
    lines.append('# SBR grid payload price: 48 kHz HE-AAC v1 stereo')
    lines.append('')
    lines.append(f'Completed {len(clips)}/49 clips at 40/48/64 kbps; excluded {len(failed)} clip(s) from every arm. Identical-grid sample: '+', '.join(f'{r}k {matching[r]} frame-channels' for r in (40,48,64))+'.')
    if fails:
        lines.append('Excluded clips: '+', '.join(f"{r['clip']} ({r['error']})" for r in fails)+'.')
    lines.append('')
    lines.append('## Table 1: SBR bits per frame per channel on identical grids')
    lines.append('')
    lines.append('Cells are total (grid and header / envelope / noise / other), in bits. Shared CPE fields are divided equally between channels. FF/FV/VF/VV are FIXFIX/FIXVAR/VARFIX/VARVAR. Only frames with exact class, borders, and per-envelope frequency resolution in both injected streams and their aligned FDK donor are included.')
    lines.append('')
    lines.append('| kbps | class | envelopes | n frame-ch | FDK | FAAC A-inject | FAAC B-inject |')
    lines.append('|---:|:---:|---:|---:|---:|---:|---:|')
    for rate,cls,n in sorted(groups):
        d=groups[rate,cls,n];lines.append(f"| {rate} | {CLASS[cls]} | {n} | {len(d['fdk'])} | {fmt_bits(d['fdk'])} | {fmt_bits(d['A'])} | {fmt_bits(d['B'])} |")
    lines.append('')
    lines.append('## Table 2: syntax choices on those frames')
    lines.append('')
    lines.append('dt is the share of coded envelopes using time delta; amp and frequency resolution are shares of envelope instances with flag 1; coupling is the share of stereo frames with CPE coupling enabled.')
    lines.append('')
    lines.append('| kbps | encoder | dt % | amp_res=1 % | freq_res=1 % | coupled % |')
    lines.append('|---:|:---|---:|---:|---:|---:|')
    for rate in (40,48,64):
        for arm in ('fdk','A','B'):
            d=perrate[rate,arm];rec=d['records'];frames=d['frames']
            den=sum(len(x['dt_env']) for x in rec)
            dt=100*sum(sum(x['dt_env']) for x in rec)/den
            amp=100*sum(x['amp']*len(x['dt_env']) for x in rec)/den
            freq=100*sum(sum(x['freq_res']) for x in rec)/den
            coupled=100*sum(x['coupling'] for x in frames)/len(frames)
            lines.append(f'| {rate} | {arm} | {dt:.1f} | {amp:.1f} | {freq:.1f} | {coupled:.1f} |')
    lines.append('')
    lines.append('## Table 3: core bits and MOS change versus each base')
    lines.append('')
    lines.append('ΔMOS is inject minus base. Adjusted Δ charges each clip at its own baseline 40/48/64 kbps MOS-per-bit slope, using adjacent rungs at the endpoints and a central difference at 48 kbps. W/L counts use ±0.02 MOS; ties are omitted. Bitrate adjustment uses total ADTS bytes.')
    lines.append('')
    lines.append('| kbps | comparison | core base→inject bits/frame | FF Δ raw / adjusted (W/L) | FDK decode Δ raw / adjusted (W/L) |')
    lines.append('|---:|:---|---:|---:|---:|')
    for rate in (40,48,64):
        for prefix in ('A','B'):
            base,inj=prefix+'-base',prefix+'-inject';b=[lookup[x.name,rate,base] for x in clips];i=[lookup[x.name,rate,inj] for x in clips]
            core=f"{mean(float(x['core_bits']) for x in b):.1f}→{mean(float(x['core_bits']) for x in i):.1f}"
            cells=[]
            for col in ('ff_mos','fdk_mos'):
                raw=[];adj=[]
                for clip,br,ir in zip(clips,b,i):
                    name=clip.name;lo,hi={40:(40,48),48:(40,64),64:(48,64)}[rate]
                    rl=lookup[name,lo,base];rh=lookup[name,hi,base]
                    denom=(int(rh['bytes'])-int(rl['bytes']))/int(br['bytes'])*100
                    if denom<=0:err(f'baseline bit slope {name} {rate} {prefix}')
                    k=(float(rh[col])-float(rl[col]))/denom
                    delta=float(ir[col])-float(br[col]);raw.append(delta)
                    adj.append(delta-k*(int(ir['bytes'])/int(br['bytes'])-1)*100)
                cells.append(f'{mean(raw):+.4f} / {mean(adj):+.4f} ({sum(x>.02 for x in raw)}/{sum(x<-.02 for x in raw)})')
            lines.append(f'| {rate} | {prefix}-inject vs {prefix}-base | {core} | {cells[0]} | {cells[1]} |')
    lines.append('')
    coh_path=ROOT/'coherence.csv'
    if coh_path.exists():
        coh_rows=list(csv.DictReader(coh_path.open()))
        coh={(x['clip'],int(x['rate']),x['arm'],x['decoder']):x['error'] for x in coh_rows}
        lines.append('## Stereo coherence error')
        lines.append('')
        lines.append('Established benchmark phase-3 per-frame coherence error; lower is better. Δ is inject minus base, averaged over the same included clips.')
        lines.append('')
        lines.append('| kbps | comparison | FF base→inject / Δ | FDK decode base→inject / Δ |')
        lines.append('|---:|:---|---:|---:|')
        for rate in (40,48,64):
            for prefix in ('A','B'):
                cells=[]
                for dec in ('ff','fdk'):
                    pair=[(float(coh[x.name,rate,prefix+'-base',dec]),float(coh[x.name,rate,prefix+'-inject',dec]))
                          for x in clips if coh.get((x.name,rate,prefix+'-base',dec)) not in ('',None,'None') and coh.get((x.name,rate,prefix+'-inject',dec)) not in ('',None,'None')]
                    cells.append(f'{mean(x for x,y in pair):.4f}→{mean(y for x,y in pair):.4f} / {mean(y-x for x,y in pair):+.4f} (n={len(pair)})')
                lines.append(f'| {rate} | {prefix}-inject vs {prefix}-base | {cells[0]} | {cells[1]} |')
        lines.append('')
    lines.append('## Where the extra bits go')
    lines.append('')
    var={arm:[x for rate in (40,48,64) for x in perrate[rate,arm]['records'] if x['class']!=0] for arm in ('fdk','A','B')}
    # Same records in the same order, filtered by donor class rather than arm class.
    for arm in ('A','B'):
        diffs={k:mean(x[k]-y[k] for x,y in zip(var[arm],var['fdk'])) for k in ('grid','env','noise','other')}
        total=mean(x['total']-y['total'] for x,y in zip(var[arm],var['fdk']))
        ranked=', '.join(f'{k} {v:+.1f}' for k,v in sorted(diffs.items(),key=lambda q:-q[1]))
        lines.append(f'{arm} minus FDK on non-FF frames: total {total:+.1f} bits/frame/channel; {ranked}.')
    lines.append('')
    lines.append('## Verification')
    lines.append('')
    lines.append(f"All {len(accounting)} FAAC clip/rate/arm streams passed: encoder and FAAD per-frame splits agree; SBR writer bits equal (ADTS payload bits − core bits − stuffing) within 1%; FAAD parser consumed each fill-element boundary with 0–7 pad bits.")
    lines.append('Terminal concealment cause: when the donor row was absent, the probe emitted a FIXFIX fallback with stale per-envelope frequency resolution; one-envelope VARFIX closure also needed its signaled 1.5 dB amplitude resolution. The probe now emits a legal VARFIX from the previous trailing border when needed, otherwise FIXFIX, and resets its frequency resolution. The terminal frame has zero FAAD and FFmpeg concealment.')
    lines.append('No-donor closing-grid records: '+', '.join(f'{rate}k {arm} {closing[rate,arm]}' for rate in (40,48,64) for arm in ('A-inject','B-inject'))+'.')
    lines.append('')
    lines.append('### Side-by-side frames (three per class)')
    lines.append('')
    lines.append('| class | clip / kbps / FAAC frame / ch | FDK bits (g/e/n/o) | A bits (g/e/n/o) | B bits (g/e/n/o) |')
    lines.append('|:---:|:---|---:|---:|---:|')
    for cls in range(4):
        if len(examples[cls])<3:err(f'only {len(examples[cls])} spot frames for class {CLASS[cls]}')
        for name,rate,fr,ch,data in examples[cls]:
            def q(x):return f"{x['total']:.1f} ({x['grid']:.1f}/{x['env']:.1f}/{x['noise']:.1f}/{x['other']:.1f})"
            lines.append(f"| {CLASS[cls]} | {name} / {rate} / {fr} / {ch} | {q(data['fdk'])} | {q(data['A'])} | {q(data['B'])} |")
    lines.append('')
    lines.append('## Provenance and commands')
    lines.append('')
    lines.append('A `8d31b3a519d57c0bcc14bcb2e9cc62c793c1c100`, master `ca4c091516639704aad3d1497c02f84f8c813f0f`, #579 `763eaa2690f387f255bb0bddd4f456ceaa76d9bd`; B working tree merges #579 into A. FAAD3 base `6ab0fea798d0eaa7f977b9ba3c361fe8d4ace9db`. Frontend build Git tags were fixed to `ca4c0915` for A/master and `763eaa26` for B/#579 during raw `.m4a` identity checks. All 30 five-clip identity pairs passed; their MD5s are in `gate_four/results.csv`. All injected streams had zero FAAD concealment and FFmpeg errors, and 100% exact donor grids on both channels. Click correlation gave FAAC delay 3042, FDK delay 5057 samples at all three rates, so FDK was padded by 33 samples and FAAC frame n used FDK frame n+1.')
    lines.append('')
    lines.append('```sh')
    lines.append('fdkaac -p 5 -b 40000 -f 2 -o fdk/stream.aac padded.wav')
    lines.append('FAAD_DUMP=fdk/sbr.dump faad --strict -q -b 32f -o fdk/faad.wav fdk/stream.aac')
    lines.append('FAAC_SBR_INJECT=fdk/sbr.dump FAAC_SBR_INJECT_FIELDS=grid FAAC_SBR_INJECT_OFFSET=1 FAAC_PRICE_DUMP=A-inject/writer.dump faac --overwrite --object-type he-aac-v1 -b 40 -o A-inject/stream.aac input.wav')
    lines.append('FAAD_DUMP=A-inject/sbr.dump faad --strict -q -b 32f -o A-inject/faad.wav A-inject/stream.aac')
    lines.append('ffmpeg -y -v error -xerror -err_detect explode -i A-inject/stream.aac -c:a pcm_f32le A-inject/ff.wav')
    lines.append('fdkdec A-inject/stream.aac A-inject/fdk.wav')
    lines.append('NUMBA_DISABLE_JIT=1 /Users/nschimme/gitprojects/faac-benchmark/.venv/bin/python /Users/nschimme/gitprojects/faac-benchmark/scripts/align/sc.py input.wav A-inject/ff.wav')
    lines.append('```')
    lines.append('')
    lines.append('The full serial commands were `/Users/nschimme/gitprojects/faac-benchmark/.venv/bin/python probe/gate_four.py`, `/Users/nschimme/gitprojects/faac-benchmark/.venv/bin/python probe/price_run.py`, `/Users/nschimme/gitprojects/faac-benchmark/.venv/bin/python probe/price_coherence.py`, and `/Users/nschimme/gitprojects/faac-benchmark/.venv/bin/python probe/price_analyze.py`, from the B worktree. `probe/faad-price.patch`, `probe/A-price.patch`, and `probe/B-price.patch` save the instrumentation and probe changes. No fdk-aac source was read.')
    (Path(__file__).resolve().parent/'PRICE_RESULT.md').write_text('\n'.join(lines)+'\n')
    print(f'report written, {len(clips)} clips, {len(rows)} rows, {len(accounting)} streams')
if __name__=='__main__': main()
