#!/usr/bin/env python3
"""Fresh FAAD3 dumps: long and grouped-short sections and SF class costs.

Scalefactor predictors continue across window groups; section runs do not.
No out-of-range delta is silently clamped. Decoder must contain 486d97ba.
"""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

def cls(book):
    return 'zero' if book == 0 else 'pns' if book == 13 else 'is' if book in (14,15) else 'reg'

def parse(path, sflen):
    out = {'long': Counter(), 'short': Counter()}
    for line in Path(path).read_text().splitlines():
        if not line.startswith('C '): continue
        head, body = line.split('|'); h = list(map(int, head.split()[1:]))
        frame, channel, bits, win, max_sfb, ngroups, gain = h
        key = 'short' if win == 2 else 'long'; c = out[key]
        groups = [[tuple(map(int, t.split(':'))) for t in group.split()] for group in body.split('/') if group.strip()]
        assert len(groups) == ngroups and all(len(g) == max_sfb for g in groups)
        c['ics'] += 1; c['groups'] += ngroups
        c['element_bits'] += bits if channel == 0 else 0
        previous = {'reg': gain, 'pns': None, 'is': 0}
        rb = 3 if win == 2 else 5; maximum = (1 << rb)-1
        for group in groups:
            i = 0
            while i < len(group):
                book = group[i][0]; j = i+1
                while j < len(group) and group[j][0] == book: j += 1
                c['sections'] += 1
                c['section_bits'] += 4+rb*(1+(j-i)//maximum)
                c['run_escape_bits'] += rb*((j-i)//maximum)
                c['sections:'+cls(book)] += 1
                if i:
                    p, q = cls(group[i-1][0]), cls(book)
                    cause = 'reg-cb' if p == q == 'reg' else '-'.join(sorted([p,q]))
                    c['break:'+cause] += 1
                i = j
            for book, sf, nnz, ms in group:
                cl = cls(book); c['bands'] += 1; c['bands:'+cl] += 1; c['ms_bands'] += bool(ms)
                if cl == 'zero': continue
                if cl == 'pns' and previous[cl] is None:
                    c['sf_bits:'+cl] += 9
                else:
                    delta = sf-previous[cl]
                    assert -60 <= delta <= 60, (path, frame, delta, cl)
                    c['sf_bits:'+cl] += sflen[delta+60]
                previous[cl] = sf
    return out

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('ladder'); ap.add_argument('decoder'); ap.add_argument('huffdata'); ap.add_argument('output')
    a = ap.parse_args(); output = Path(a.output); output.mkdir(parents=True, exist_ok=True)
    source = Path(a.huffdata).read_text().split('book12')[1].split('};')[0]
    sflen = [int(x) for x in re.findall(r'\{\s*(\d+)\s*,', source)]
    assert len(sflen) == 121
    report = {'decoder_sha256': hashlib.sha256(Path(a.decoder).read_bytes()).hexdigest(), 'source_fix': '486d97ba', 'rows': {}}
    for rate in ['he48','lc128']:
        for encoder in ['stack','fdk','apple']:
            streams = sorted((Path(a.ladder)/rate/encoder).glob('*.m4a'))
            assert len(streams) == 49, (rate, encoder, len(streams))
            totals = {'long': Counter(), 'short': Counter()}
            hashes = {}
            for stream in streams:
                dump = output/f'{rate}-{encoder}-{stream.stem}.dump'
                # FAAD appends. An existing dump is replaced explicitly, never reused.
                dump.write_text('')
                with tempfile.TemporaryDirectory() as tmp:
                    subprocess.run([a.decoder, '-o', str(Path(tmp)/'decoded.wav'), str(stream)],
                        env=dict(os.environ, FAAD_DUMP=str(dump.resolve())), capture_output=True, check=True)
                result = parse(dump, sflen)
                assert sum(x['ics'] for x in result.values()) > 0
                for key in totals: totals[key].update(result[key])
                hashes[stream.name] = hashlib.sha256(stream.read_bytes()).hexdigest()
            n = totals['long']['ics']+totals['short']['ics']
            report['rows'][f'{rate}-{encoder}'] = dict(totals=totals, long_ics_fraction=totals['long']['ics']/n, streams=hashes)
            print(rate, encoder, 'long coverage', round(totals['long']['ics']/n,4),
                {key: {field: round(totals[key][field]/max(1,totals[key]['ics']),3) for field in ['sections','section_bits','sf_bits:reg','sf_bits:pns','sf_bits:is']} for key in totals}, flush=True)
    (output/'summary.json').write_text(json.dumps(report, indent=2)+'\n')

if __name__ == '__main__': main()
