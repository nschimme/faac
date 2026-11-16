#!/usr/bin/env python3
"""Verify every reference stream's sha256/size against its manifest; write stage0/stream_hashes.md."""
import hashlib, json, os
from multiprocessing import Pool
REF = '/home/user/faac/probe/ladder/ref'
OUT = '/home/user/faac/probe/ladder/results/s12-apple48k/stage0/stream_hashes.md'
SETS = ['apple-48k', 'fdk-48k', 'xaac-48k']

def chk(a):
    d, r = a
    p = f'{REF}/{d}/{r["path"]}'
    if not os.path.exists(p):
        return d, r['path'], 'missing'
    b = open(p, 'rb').read()
    if len(b) != r['size']:
        return d, r['path'], f'size {len(b)} != {r["size"]}'
    if hashlib.sha256(b).hexdigest() != r['sha256']:
        return d, r['path'], 'sha256 mismatch'
    return d, r['path'], None

if __name__ == '__main__':
    jobs = []
    for d in SETS:
        for r in json.load(open(f'{REF}/{d}/manifest.json'))['rows']:
            jobs.append((d, r))
    with Pool(2) as p:
        res = p.map(chk, jobs, chunksize=16)
    L = ['# Reference stream integrity (size + sha256 vs manifest)', '', '| set | manifest rows | ok | bad |', '|---|---|---|---|']
    bad = [x for x in res if x[2]]
    for d in SETS:
        n = sum(1 for x in res if x[0] == d); b = sum(1 for x in bad if x[0] == d)
        L.append(f'| {d} | {n} | {n-b} | {b} |')
    # files on disk not in manifest
    for d in SETS:
        inman = {r['path'] for r in json.load(open(f'{REF}/{d}/manifest.json'))['rows']}
        ondisk = {os.path.relpath(os.path.join(rt, f), f'{REF}/{d}') for rt, _, fs in os.walk(f'{REF}/{d}') for f in fs if f.endswith('.m4a')}
        L.append(f'\n{d}: on disk not in manifest: {len(ondisk-inman)}; in manifest not on disk: {len(inman-ondisk)}')
    if bad:
        L += ['', '## Mismatches', ''] + [f'- {d}/{p}: {w}' for d, p, w in bad]
    else:
        L += ['', 'No mismatches.']
    open(OUT, 'w').write('\n'.join(L) + '\n'); print('\n'.join(L))
