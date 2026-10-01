#!/usr/bin/env python3
"""FAAM byte regression: MASTER_FAAC=/path/to/faac python3 tests/compare_master_mp4.py."""
import argparse
import math
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import wave


def normalize(data, custom_values=None):
    """Ignore git hash; repair only master's undefined custom-tag value bytes.

    Master borrows a freed UTF-8 buffer for --tag. The authorized exception
    substitutes the expected live value and adjusts its enclosing sizes;
    mean/name/data headers, atom order, and all other bytes remain compared.
    The candidate never receives this repair.
    """
    # The only ignored payload is the encoder tag; preserve every other byte.
    import re
    def boxes(buf):
        result = bytearray()
        pos = 0
        while pos < len(buf):
            size, kind = struct.unpack_from('>I4s', buf, pos)
            if size < 8 or pos + size > len(buf):
                raise ValueError('invalid box')
            content = buf[pos+8:pos+size]
            if kind in [b'moov', b'udta', b'ilst']:
                content = boxes(content)
            elif kind == b'meta':
                content = content[:4] + boxes(content[4:])
            elif kind == b'----' and custom_values:
                fields = []
                p = 0
                while p < len(content):
                    n, tag = struct.unpack_from('>I4s', content, p)
                    if n < 8 or p+n > len(content):
                        raise ValueError('invalid custom tag')
                    fields.append((tag, content[p+8:p+n]))
                    p += n
                if [tag for tag, _ in fields] != [b'mean', b'name', b'data']:
                    raise ValueError('unexpected freeform tag layout')
                if fields[0][1] == b'\0\0\0\0faac':
                    key = fields[1][1][4:].decode('utf-8')
                    if key not in custom_values:
                        raise ValueError(f'unexpected custom tag {key}')
                    tag, value = fields[2]
                    fields[2] = (tag, value[:8] + custom_values[key].encode('utf-8'))
                    content = b''.join(struct.pack('>I4s',8+len(v),t)+v for t,v in fields)
            elif kind == b'\xa9too':
                encoder = content[16:]
                if not re.fullmatch(rb'FAAC [^ ()]+ \([0-9a-f]+(?:-dirty)?\)', encoder):
                    raise ValueError(f'unexpected encoder tag {encoder!r}')
                encoder = re.sub(rb'\([0-9a-f]+(?:-dirty)?\)', b'(HASH)', encoder)
                content = struct.pack('>I', 16+len(encoder)) + content[4:16] + encoder
            result += struct.pack('>I4s',8+len(content),kind)+content
            pos += size
        return bytes(result)
    return boxes(data)


def main():
    ap = argparse.ArgumentParser(__doc__)
    ap.add_argument('--candidate', default='build/frontend/faac')
    ap.add_argument('--artifacts', type=Path, help='retain differing raw and normalized files')
    args = ap.parse_args()
    master = os.environ['MASTER_FAAC']
    if args.artifacts:
        args.artifacts.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='faam-compare-') as td:
        root = Path(td)
        for name, rate, channels, seconds in [('stereo',44100,2,3), ('mono',48000,1,3), ('short',44100,2,.2), ('surround',48000,6,3)]:
            with wave.open(str(root / (name+'.wav')), 'wb') as w:
                w.setparams((channels,2,rate,0,'NONE','not compressed'))
                pcm = bytearray()
                for i in range(int(rate*seconds)):
                    for ch in range(channels):
                        value = int((7000+3000*math.sin(i/rate*9))*math.sin(2*math.pi*(220+ch*131)*i/rate))
                        pcm += struct.pack('<h',value)
                w.writeframes(pcm)
        cover = root / 'cover.png'
        import base64
        cover.write_bytes(base64.b64decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+a9WQAAAAASUVORK5CYII='))
        tags = ['--artist','Artist','--artistsort','Artist sort','--composer','Composer','--composersort','Composer sort','--title','Title','--album','Album','--albumartist','Album artist','--albumartistsort','Album artist sort','--albumsort','Album sort','--year','2026','--comment','Comment','--genre','17','--compilation','--track','2/7','--disc','1/2','--lang','ENG','--creation-time','123456789','--cover-art',str(cover),'--tag','key,value']
        cases = []
        for name in ['stereo','mono','short','surround']:
            for label, flags in [('abr96',['-b','96']),('mpeg4',['-b','64','--mpeg-vers','4']),('vbr',['-q','100']),('cbr',['-b','128','--cbr']),('he',['-b','32','--object-type','he-aac-v1'])]:
                cases.append((name+'-'+label,name,flags))
        cases += [('tags','stereo',['-b','96']+tags[:-2]),
                  ('short-cbr-tags','short',['-b','128','--cbr']+tags[:-2]),
                  ('custom-tags','stereo',['-b','96']+tags),
                  ('short-cbr-custom-tags','short',['-b','128','--cbr']+tags)]
        failures = 0
        for label, name, flags in cases:
            outputs = []
            for binary, suffix in [(master,'master'),(args.candidate,'candidate')]:
                output = root/(label+'-'+suffix+'.m4a')
                output.unlink(missing_ok=True)
                result = subprocess.run([binary,*flags,'-o',str(output),str(root/(name+'.wav'))],capture_output=True)
                if result.returncode:
                    raise RuntimeError(f'{label}: {binary}: {result.stderr.decode()}')
                if label.endswith('-he') and b'HE-AAC v1' not in result.stdout + result.stderr:
                    raise RuntimeError(f'{label}: HE-AAC v1 profile was not confirmed')
                outputs.append(normalize(output.read_bytes(), {'key': 'value'} if suffix == 'master' and '--tag' in flags else None))
            a,b = outputs
            normalized = []
            for suffix, content in [('master', a), ('candidate', b)]:
                path = root/(label+'-'+suffix+'.normalized.m4a')
                path.write_bytes(content)
                normalized.append(path)
            comparison = subprocess.run(['cmp', '-s', *map(str, normalized)])
            if comparison.returncode:
                first = next((i for i,(x,y) in enumerate(zip(a,b)) if x!=y),min(len(a),len(b)))
                if args.artifacts:
                    for suffix, content in [('master', a), ('candidate', b)]:
                        (args.artifacts/(label+'-'+suffix+'.normalized.m4a')).write_bytes(content)
                        (args.artifacts/(label+'-'+suffix+'.m4a')).write_bytes((root/(label+'-'+suffix+'.m4a')).read_bytes())
                print(f'FAIL {label}: lengths {len(a)}/{len(b)}, first offset {first}: {a[first:first+16].hex()}/{b[first:first+16].hex()}')
                failures += 1
            else:
                print(f'PASS {label}')
        print(f'{len(cases)} combos, {len(cases)-failures} identical except git hash and documented master custom-value exception, {failures} failures')
        return bool(failures)

if __name__ == '__main__':
    raise SystemExit(main())
