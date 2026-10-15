#!/usr/bin/env python3
"""Strict stream/PCM equivalence audit for upstream-master based HE-v1 branches."""
from __future__ import annotations

import concurrent.futures
import hashlib
import json
import os
import subprocess
import sys
import tempfile
import threading
import wave
from pathlib import Path

ROOT = Path('/Users/nschimme/gitprojects/faac')
BENCH = Path('/Users/nschimme/gitprojects/faac-benchmark')
OUT = ROOT / '.cache/he-v1-feature-study/unstack-validation'
BASE = Path('/private/tmp/faac-pr-baseline/build-pub/frontend/faac')
BASE_REPO = Path('/private/tmp/faac-pr-baseline')
UPSTREAM = '01e7aa7f078095a69e8423578f8abab8ae61ec70'
JOBS = 4
CLIPS = sorted(p.name for p in (BENCH / 'data/external/audio').glob('*.wav'))
RATES = [32, 48, 64, 96]
GROUPS = {
    'baseline': {'binary': BASE, 'repo': BASE_REPO},
    'noise': {'binary': Path('/private/tmp/faac-pr-noise/build-pub/frontend/faac'), 'repo': Path('/private/tmp/faac-pr-noise')},
    'limiter': {'binary': Path('/private/tmp/faac-pr-limiter/build-pub/frontend/faac'), 'repo': Path('/private/tmp/faac-pr-limiter')},
    'envelope': {'binary': Path('/private/tmp/faac-pr-envelope/build-pub/frontend/faac'), 'repo': Path('/private/tmp/faac-pr-envelope')},
    'combined': {'binary': Path('/private/tmp/faac-pr-combined/build-pub/frontend/faac'), 'repo': Path('/private/tmp/faac-pr-combined')},
}
REFS = {
    'baseline': ('/private/tmp/faac-he-v1-probes/probe/results-all/feature-results.json', 'baseline_default'),
    'noise': ('/Users/nschimme/gitprojects/faac/.cache/he-v1-feature-study/full/feature-results.json', 'noise_18'),
    'limiter': ('/private/tmp/faac-he-v1-probes/probe/results-limiter/candidate-equivalence.json', 'limiter_gain_3'),
    'envelope': (str(ROOT / '.cache/he-v1-feature-study/envelope-count/full-active-lambda/envcount-results.json'), 'hybrid_envelope'),
    'combined': (str(ROOT / '.cache/he-v1-feature-study/envelope-count/full-active-lambda/envcount-results.json'), 'hybrid_combined'),
}

def sha(path: Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda: f.read(1024 * 1024), b''):
            h.update(b)
    return h.hexdigest()

def run(cmd, env=None):
    return subprocess.run(cmd, check=True, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)

def packet_sha(path: Path) -> str:
    p = run(['ffprobe', '-v', 'error', '-select_streams', 'a:0', '-show_packets', '-show_data_hash', 'sha256', '-show_entries', 'packet=size,data_hash', '-of', 'json', str(path)])
    packets = json.loads(p.stdout).get('packets', [])
    if not packets:
        raise RuntimeError(f'no AAC packets in {path}')
    h = hashlib.sha256()
    for x in packets:
        h.update(str(int(x['size'])).encode() + b':' + x['data_hash'].encode() + b'\n')
    return h.hexdigest()

def profile(path: Path) -> str:
    p = run(['ffprobe', '-v', 'error', '-select_streams', 'a:0', '-show_entries', 'stream=profile', '-of', 'json', str(path)])
    rows = json.loads(p.stdout).get('streams', [])
    if len(rows) != 1:
        raise RuntimeError(f'expected one audio stream: {path}')
    return rows[0].get('profile', '')

def dur(path: Path) -> float:
    with wave.open(str(path), 'rb') as w:
        return w.getnframes() / w.getframerate()

def ref_variant(group, rate):
    if group == 'envelope':
        return 'tier_base' if rate == 32 else 'all_base'
    if group == 'combined':
        return 'stack_reference' if rate == 32 else 'all_stack'
    return REFS[group][1]

def source_row(group, clip, rate, sources):
    source = sources[group]
    v = ref_variant(group, rate)
    if group == 'limiter':
        row = source['rows'].get(f'{v}|{clip}|{rate}')
        if not row:
            raise RuntimeError(f'missing historical limiter row {v}/{clip}/{rate}')
        return {'payload_sha256': row['candidate_packet_sha256'], 'decoded_sha256': row['candidate_decoded_sha256'],
                'mos': row.get('inherited_mos'), 'mos_source': row.get('mos_source'), 'comparison_variant': row.get('comparison_variant'),
                'source_file': str(REFS[group][0]), 'source_key': row.get('key')}
    row = source['rows'].get(f'{v}|{clip}|{rate}')
    if not row:
        raise RuntimeError(f'missing historical {group} row {v}/{clip}/{rate}')
    packet = row.get('payload_sha256') or row.get('candidate_packet_sha256')
    decoded = row.get('decoded_sha256') or row.get('candidate_decoded_sha256')
    return {'payload_sha256': packet, 'decoded_sha256': decoded,
            'mos': row.get('mos'), 'mos_source': row.get('mos_source'), 'source_file': str(REFS[group][0]), 'source_key': f'{v}|{clip}|{rate}'}

def patch_ids(repo: Path):
    head = subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'],text=True).strip()
    if head == UPSTREAM:
        return []
    commits = subprocess.check_output(['git','-C',str(repo),'rev-list','--reverse',UPSTREAM+'..HEAD'],text=True).splitlines()
    out = []
    for commit in commits:
        patch = subprocess.run(['git','-C',str(repo),'show','--pretty=format:','--no-ext-diff',commit],check=True,stdout=subprocess.PIPE).stdout
        pid = subprocess.run(['git','patch-id','--stable'],input=patch,check=True,stdout=subprocess.PIPE).stdout.decode().split()
        out.append({'commit':commit,'patch_id':pid[0] if pid else None})
    return out

def main():
    if len(CLIPS) != 49:
        raise RuntimeError(f'expected 49 source clips, found {len(CLIPS)}')
    sys.path.insert(0, str(BENCH))
    from codec_bench.encoders import FAACEncoder
    from codec_bench.decoders import FDKDecoder
    import utils
    decoder = FDKDecoder('FDK AAC strict', str(BENCH / 'bin/fdkdec'))
    source_dir = BENCH / 'data/external/audio'
    sources = {g: json.loads(Path(path).read_text()) for g, (path, _) in REFS.items()}
    envlist = []
    for group, cfg in GROUPS.items():
        binary = cfg['binary']
        repo = cfg['repo']
        if not binary.is_file():
            raise RuntimeError(f'missing binary {binary}')
        libs = [repo / 'build-pub/libfaac/libfaac.3.dylib', repo / 'build-pub/libfaam/libfaam.3.dylib']
        envlist.append({'group': group, 'binary': str(binary), 'binary_sha256': sha(binary), 'source_head': subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'],text=True).strip(),
                        'source_base': UPSTREAM, 'source_patch_sha256': hashlib.sha256(subprocess.check_output(['git','-C',str(repo),'diff','--binary',UPSTREAM+'...HEAD'])).hexdigest(),
                        'commit_patch_ids':patch_ids(repo),
                        'linked_libs': [{'path':str(p),'sha256':sha(p)} for p in libs]})
    output = OUT / 'results.json'
    OUT.mkdir(parents=True, exist_ok=True)
    rows = {}
    if output.exists():
        rows = json.loads(output.read_text()).get('rows', {})
    # Normalize any previously checkpointed rows to the authoritative raw MOS
    # ledger and reassert identity; these are hashes only, so no audio is kept.
    for key, row in rows.items():
        if row.get('status') != 'pass':
            continue
        ref = source_row(row['group'], row['clip'], int(row['target_kbps']), sources)
        if row.get('payload_sha256') != ref['payload_sha256'] or row.get('decoded_sha256') != ref['decoded_sha256']:
            raise RuntimeError(f'checkpointed row no longer matches historical reference: {key}')
        row['reference'] = ref
        row['mos_inherited'] = ref.get('mos')
        row['mos_inheritance_eligible'] = True
    lock = threading.Lock()
    temp_root = OUT / 'artifacts'
    temp_root.mkdir(exist_ok=True)
    tasks = [(g,c,r) for g in GROUPS for c in CLIPS for r in RATES]
    def one(task):
        group, clip, rate = task
        key = f'{group}|{clip}|{rate}'
        existing = rows.get(key)
        if existing and existing.get('status') == 'pass':
            print(f'resume {key}', flush=True); return
        src = source_dir / clip
        enc = temp_root / group / str(rate) / (Path(clip).stem + '.m4a')
        wav = temp_root / group / str(rate) / (Path(clip).stem + '.wav')
        enc.parent.mkdir(parents=True, exist_ok=True)
        wav.parent.mkdir(parents=True, exist_ok=True)
        encoder = FAACEncoder(f'faac-unstack-{group}', str(GROUPS[group]['binary']), profile='he')
        cmd = encoder.get_encode_cmd(str(src), str(enc), rate, 2, 48000)
        if '--object-type' not in cmd or cmd[cmd.index('--object-type')+1] != 'he-aac-v1':
            raise RuntimeError(f'command did not force HE v1: {cmd}')
        env = dict(os.environ)
        for k in list(env):
            if k.startswith('FAAC_SBR_'):
                env.pop(k)
        run(cmd, env=env)
        prof = profile(enc)
        if prof != 'HE-AAC':
            raise RuntimeError(f'{key}: unexpected ffprobe profile {prof}')
        pcm = temp_root / group / str(rate) / (Path(clip).stem + '.fdk.wav')
        de = dict(os.environ); de.update(decoder.get_run_env())
        run(decoder.get_decode_cmd(str(enc), str(pcm)), env=de)
        if not pcm.is_file() or pcm.stat().st_size == 0:
            raise RuntimeError(f'{key}: FDK produced no PCM')
        if not utils.wav_conv(str(pcm), str(wav), rate=48000, channels=2):
            raise RuntimeError(f'{key}: FDK conversion failed')
        packet = packet_sha(enc); pcmhash = sha(wav)
        with wave.open(str(src), 'rb') as sw:
            duration = sw.getnframes()/sw.getframerate()
        p = run(['ffprobe','-v','error','-select_streams','a:0','-show_packets','-show_entries','packet=size','-of','json',str(enc)])
        bits = sum(int(x['size']) for x in json.loads(p.stdout)['packets'])*8.0/duration/1000.0
        ref = source_row(group, clip, rate, sources)
        row = {'group':group,'clip':clip,'target_kbps':rate,'status':'pass' if packet == ref['payload_sha256'] and pcmhash == ref['decoded_sha256'] else 'mismatch',
               'profile':prof,'payload_kbps':bits,'payload_sha256':packet,'decoded_sha256':pcmhash,
               'packet_equal':packet == ref['payload_sha256'],'pcm_equal':pcmhash == ref['decoded_sha256'],
               'reference':ref,'reference_mos_inherit_allowed':packet == ref['payload_sha256'] and pcmhash == ref['decoded_sha256'],
               'mos_source':ref.get('mos_source') if packet == ref['payload_sha256'] and pcmhash == ref['decoded_sha256'] else None}
        with lock:
            rows[key] = row
            doc = {'schema':1,'source_base':UPSTREAM,'clips':CLIPS,'rates_kbps':RATES,'strict_decoder':'faac-benchmark FDKDecoder; no fallback','forced_profile':'HE-AAC v1; ffprobe HE-AAC required','groups':envlist,'rows':rows}
            tmp = output.with_suffix('.json.tmp'); tmp.write_text(json.dumps(doc,indent=2,sort_keys=True)+'\n'); os.replace(tmp,output)
        print(f'{row["status"]} {key} packet={row["packet_equal"]} pcm={row["pcm_equal"]} rate={bits:.3f}',flush=True)
        # Hashes and exact match booleans are sufficient for this audit; avoid
        # retaining nearly a gigabyte of transient AAC and PCM per candidate.
        enc.unlink(missing_ok=True); wav.unlink(missing_ok=True); pcm.unlink(missing_ok=True)
    failures=[]
    with concurrent.futures.ThreadPoolExecutor(max_workers=JOBS) as pool:
        futs={pool.submit(one,t):t for t in tasks}
        for f in concurrent.futures.as_completed(futs):
            try: f.result()
            except Exception as e:
                failures.append((futs[f],repr(e)))
                print(f'ERROR {futs[f]} {e}',flush=True)
    summary={g:{'rows':sum(1 for x in rows.values() if x.get('group')==g),'mismatches':sum(1 for x in rows.values() if x.get('group')==g and x.get('status')=='mismatch')} for g in GROUPS}
    summary['total_rows']=sum(x['rows'] for x in summary.values())
    summary['task_errors']=[{'task':str(t),'error':e} for t,e in failures]
    (OUT/'summary.json').write_text(json.dumps(summary,indent=2,sort_keys=True)+'\n')
    doc = {'schema':1,'source_base':UPSTREAM,'clips':CLIPS,'clip_sha256':{c:sha(source_dir/c) for c in CLIPS},
           'rates_kbps':RATES,'strict_decoder':'faac-benchmark FDKDecoder; no fallback',
           'fdk':{'binary':str(BENCH/'bin/fdkdec'),'binary_sha256':sha(BENCH/'bin/fdkdec')},
           'forced_profile':'FAACEncoder profile=he, command --object-type he-aac-v1; ffprobe HE-AAC required',
           'mos_policy':'No MOS was recalculated. Historical MOS may be inherited only for packet and converted FDK PCM exact matches.',
           'groups':envlist,'rows':rows}
    tmp=output.with_suffix('.json.tmp'); tmp.write_text(json.dumps(doc,indent=2,sort_keys=True)+'\n'); os.replace(tmp,output)
    print(json.dumps(summary,indent=2),flush=True)
    return 1 if failures or any(summary[g]['mismatches'] for g in GROUPS) or summary['total_rows'] != 980 else 0

if __name__ == '__main__':
    raise SystemExit(main())
