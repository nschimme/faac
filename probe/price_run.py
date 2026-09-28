#!/usr/bin/env python3
"""Serial, fail-fast SBR price probe. Every completed clip/rate has a marker."""
import csv, os, re, subprocess, sys
from pathlib import Path
import numpy as np
import soundfile as sf

sys.path.insert(0, '/private/tmp/claude-501/faac-work/transplant/probe')
from grid_score import grid_match

HERE = Path(__file__).resolve().parent
ROOT = HERE / 'run'
CORPUS = Path('/Users/nschimme/gitprojects/faac-benchmark/data/external/audio')
PYTHON = Path('/Users/nschimme/gitprojects/faac-benchmark/.venv/bin/python')
SCORE = Path('/Users/nschimme/gitprojects/faac-benchmark/scripts/align/sc.py')
FAAD = Path('/private/tmp/claude-501/faac-work/faad3/bs/frontend/faad')
FDKDEC = Path('/Users/nschimme/gitprojects/faac-benchmark/bin/fdkdec')
FAAC = {'A': Path('/private/tmp/price-grid-pristine-A/build/frontend/faac'),
        'B': Path('/private/tmp/claude-501/faac-work/price-grid/build/frontend/faac')}
ARMS = ('A-base', 'A-inject', 'B-base', 'B-inject')

class ConcealmentError(RuntimeError):
    pass
FIELDS = ['clip','rate','arm','bytes','ff_mos','fdk_mos','core_bits','sbr_bits','grid0','grid1']

def fail(which):
    raise RuntimeError('HARNESS ERROR: ' + which)

def run(cmd, env=None):
    e={k:v for k,v in os.environ.items() if not k.startswith('FAAC_SBR_INJECT') and k!='FAAC_PRICE_DUMP' and k!='FAAD_DUMP'}
    e.update(env or {})
    p=subprocess.run([str(x) for x in cmd],env=e,text=True,capture_output=True)
    if p.returncode: fail(f'command {cmd}: {p.stderr[-1600:]}')
    return p

def profile(stream):
    text=run(['ffprobe','-v','error','-select_streams','a:0','-show_entries','stream=profile,sample_rate,channels','-of','default=noprint_wrappers=1',stream]).stdout
    if not all(x in text for x in ('profile=HE-AAC','sample_rate=48000','channels=2')): fail(f'profile {stream}: {text}')

def price_dump(stream, d):
    (d/'sbr.dump').unlink(missing_ok=True)
    p=run([FAAD,'--strict','-q','-b','32f','-o',d/'faad.wav',stream],{'FAAD_DUMP':str(d/'sbr.dump')})
    if not re.search(r'Error concealment\s+: 0 frames',p.stderr) or not re.search(r'non-END termination: 0',p.stderr):
        raise ConcealmentError(f'FAAD concealment {stream}: {p.stderr[-1000:]}')
    records={}
    for line in (d/'sbr.dump').read_text().splitlines():
        if not line.startswith('P '): continue
        w=line.split(' |')[0].split();fr,ch=int(w[1]),int(w[2]);total=float(w[3]);ext=int(w[13]);pad=int(w[14]);
        if pad>7 or pad<0: fail(f'FAAD parser boundary {stream} frame {fr}: pad={pad}')
        records[(fr,ch)]=w
    if not records: fail(f'no FAAD price records {stream}')
    for fr in {key[0] for key in records}:
        if (fr,0) not in records or (fr,1) not in records: fail(f'FAAD channel price pair {stream} frame {fr}')
        p0,p1=records[fr,0],records[fr,1]
        if abs(float(p0[3])+float(p1[3])-int(p0[13]))>0.01 or p0[13]!=p1[13]:
            fail(f'FAAD parsed bit sum {stream} frame {fr}: {p0[3]}+{p1[3]} vs {p0[13]}')
    return records

def ff_decode(stream, wav):
    try:
        run(['ffmpeg','-y','-v','error','-xerror','-err_detect','explode','-i',stream,'-c:a','pcm_f32le',wav])
    except RuntimeError as exc:
        raise ConcealmentError(str(exc)) from exc

def scorer(ref,wav):
    p=run([PYTHON,SCORE,ref,wav],{'NUMBA_DISABLE_JIT':'1'})
    a=re.findall(r'[-+]?\d+(?:\.\d+)?',p.stdout)
    if not a: fail(f'ViSQOL no score: {ref} {wav}: {p.stdout}')
    return float(a[-1])

def core_dump(path):
    rows=[x.split() for x in path.read_text().splitlines() if x.startswith('C ')]
    if not rows: fail(f'no core records {path}')
    return sum(int(x[4]) for x in rows)/len(rows), sum(int(x[3]) for x in rows)/len(rows)

def check_writer(d):
    parsed={}
    for line in (d/'sbr.dump').read_text().splitlines():
        if line.startswith('P '):
            w=line.split(' |')[0].split();parsed[int(w[1]),int(w[2])]=tuple(map(float,w[3:8]))
    writes={}
    for line in (d/'writer.dump').read_text().splitlines():
        if line.startswith('W '):
            w=line.split(' |')[0].split();writes[int(w[1]),int(w[2])]=tuple(map(float,w[3:8]))
    if not writes or parsed.keys()!=writes.keys(): fail(f'encoder/decoder price frame keys {d}: {len(writes)} vs {len(parsed)}')
    for key in writes:
        if writes[key]!=parsed[key]: fail(f'encoder/decoder price split {d} frame {key}: {writes[key]} vs {parsed[key]}')

def main():
    ROOT.mkdir(parents=True,exist_ok=True)
    clips=sorted(CORPUS.glob('*.wav'))
    if len(clips)!=49: fail(f'expected 49 48k corpus clips, got {len(clips)}')
    for ci,ref in enumerate(clips):
        x,sr=sf.read(ref,dtype='float32',always_2d=True)
        if sr!=48000 or x.shape[1]!=2: fail(f'48k stereo corpus {ref} {sr} {x.shape}')
        cd=ROOT/f'{ci:02d}_{ref.stem}';cd.mkdir(exist_ok=True)
        if (cd/'done').exists(): continue
        padded=cd/'padded.wav'
        if not padded.exists(): sf.write(padded,np.vstack((np.zeros((33,2),dtype=np.float32),x)),48000,subtype='FLOAT')
        clip_rows=[]
        try:
         for rate in (40,48,64):
             d=cd/str(rate);d.mkdir(exist_ok=True)
             if (d/'done').exists(): continue
             fdk=d/'fdk';fdk.mkdir(exist_ok=True);stream=fdk/'stream.aac'
             run(['fdkaac','-p','5','-b',rate*1000,'-f','2','-o',stream,padded]);profile(stream)
             price_dump(stream,fdk)
             rows=[]
             for arm in ARMS:
                 ad=d/arm;ad.mkdir(exist_ok=True);aac=ad/'stream.aac'
                 arm_base=arm[0];env={'FAAC_PRICE_DUMP':str(ad/'writer.dump')}
                 if arm.endswith('inject'):
                     env.update({'FAAC_SBR_INJECT':str(fdk/'sbr.dump'),'FAAC_SBR_INJECT_FIELDS':'grid','FAAC_SBR_INJECT_OFFSET':'1'})
                 (ad/'writer.dump').unlink(missing_ok=True)
                 run([FAAC[arm_base],'--overwrite','--object-type','he-aac-v1','-b',rate,'-o',aac,ref],env)
                 profile(aac);price_dump(aac,ad);check_writer(ad)
                 ff_decode(aac,ad/'ff.wav')
                 run([FDKDEC,aac,ad/'fdk.wav'])
                 if arm.endswith('inject'):
                     grid=grid_match(fdk/'sbr.dump',ad/'sbr.dump')
                     if min(grid)<98: fail(f'grid {ref.name} {rate} {arm}: {grid}')
                 else: grid=[float('nan')]*2
                 cb,sb=core_dump(ad/'writer.dump')
                 rows.append({'clip':ref.name,'rate':rate,'arm':arm,'bytes':aac.stat().st_size,
                              'ff_mos':scorer(ref,ad/'ff.wav'),'fdk_mos':scorer(ref,ad/'fdk.wav'),
                              'core_bits':cb,'sbr_bits':sb,'grid0':grid[0],'grid1':grid[1]})
                 print(f'{ci+1}/49 {rate} {arm} ff={rows[-1]["ff_mos"]:.3f} fdk={rows[-1]["fdk_mos"]:.3f}',flush=True)
             clip_rows.extend(rows)
        except ConcealmentError as exc:
            with (ROOT/'failures.csv').open('a',newline='') as f:
                w=csv.writer(f)
                if f.tell()==0:w.writerow(('clip','error'))
                w.writerow((ref.name,str(exc)))
            print(f'EXCLUDE {ref.name}: {exc}',flush=True)
            if sum(1 for _ in csv.DictReader((ROOT/'failures.csv').open()))>3:
                fail('more than 3 clips concealed')
            continue
        with (ROOT/'scores.csv').open('a',newline='') as f:
            w=csv.DictWriter(f,fieldnames=FIELDS)
            if f.tell()==0:w.writeheader()
            w.writerows(clip_rows)
        (cd/'done').write_text('ok\n')
if __name__=='__main__':
    try: main()
    except Exception as ex:
        print(str(ex),file=sys.stderr,flush=True)
        sys.exit(2)
