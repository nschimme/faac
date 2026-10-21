#!/usr/bin/env python3
"""Generated fill elements; expectations derive only from octave gain formulas."""
import math
import os
import struct
import subprocess
import sys
import tempfile
import wave

from faad_frontend_test import make_wav
from faad_drc_stream import drc, inject


def main():
    faac, probe = sys.argv[1:3]
    enabled, sbr = sys.argv[3:5]
    enabled = enabled == 'true'
    case = sys.argv[5] if len(sys.argv) > 5 else 'all'
    channels = min(2, int(sys.argv[6]))
    if case == 'excluded' and channels < 2:
        return sys.exit(77)
    if case in ('he', 'hetilt') and sbr != 'true':
        return sys.exit(77)
    def selected(name):
        return case in ('all', name)
    with tempfile.TemporaryDirectory() as directory:
        src = os.path.join(directory, 'in.wav')
        make_wav(src, channels, secs=0.35)
        aac = os.path.join(directory, 'in.aac')
        subprocess.run([faac, '-a', '--object-type', 'lc', '-o', aac, src], check=True, capture_output=True)
        original = open(aac, 'rb').read()
        def decode(data, cut=0, boost=0, target=0, floating=True, flush=0, raw=False):
            path = os.path.join(directory, 'case.aac')
            with open(path, 'wb') as f:
                f.write(data)
            result = subprocess.run([probe, path, str(cut), str(boost), str(target), str(int(floating)), str(flush)] + (['raw'] if raw else []), capture_output=True)
            assert result.returncode == 0, result.returncode
            records = []
            pos = 0
            while pos < len(result.stdout):
                r = struct.unpack_from('=7I', result.stdout, pos)
                pos += 28
                pcm = result.stdout[pos:pos + r[2]]
                pos += r[2]
                assert r[0] == 0 and r[2] > 0, r
                records.append((r, pcm))
            assert len(records) >= 8
            return records
        def samples(records, channel=None):
            data = b''.join(p for _, p in records[3:])
            v = struct.unpack('=' + 'f' * (len(data) // 4), data)
            return v if channel is None else v[channel::channels]
        def ratio(actual, reference, expected, tolerance=.05):
            a, b = sum(x*x for x in actual), sum(x*x for x in reference)
            assert b > 1e-8
            db = 10 * math.log10(a / b)
            assert abs(db - expected) < tolerance, (db, expected)
        def flags(records, active, info=None):
            # bit 5: gain applied; bit 6: the frame carried dynamic_range_info
            info = active if info is None else info
            assert all(bool(r[3] & 32) == (active and enabled) and bool(r[3] & 64) == (info and enabled) for r, _ in records)
        reference = decode(original)
        if case == 'oversized':
            # A fill that claims more bytes than the frame holds is malformed whatever it
            # carries; the DRC parse must not change how the decoder handles it.
            drc_fill = decode(inject(original, '10111000', before=False, count=20), 127)
            other_fill = decode(inject(original, '10101000', before=False, count=20), 127)
            assert [p for _, p in drc_fill] == [p for _, p in other_fill], 'DRC payload altered oversized fill handling'
            return

        if selected('defaults'):
            for floating in (False, True):
                assert [p for _, p in decode(inject(original, drc()), floating=floating)] == [p for _, p in decode(original, floating=floating)]
            assert [p for _,p in decode(inject(original, drc() + '0'*160))] == [p for _,p in reference]
            print('defaults: PASS')
        if selected('gain'):
            for sign, cut, boost in ((1,127,0), (1,64,0), (0,0,127), (0,0,64)):
                result = decode(inject(original, drc(sign)), cut, boost)
                expected = (-(cut / 127) if sign else boost / 127) * 20 * math.log10(2) if enabled else 0
                ratio(samples(result), samples(reference), expected)
                flags(result, True)
                if not enabled:
                    assert [p for _, p in result] == [p for _, p in reference]
            print('known gain and frame flags: PASS')
        if selected('flags'):
            flags(decode(inject(original, drc(ctl=0)), cut=127), False, True)
            flags(decode(inject(original, drc()), cut=127), True)
            first_end = ((original[3]&3)<<11) | original[4]<<3 | original[5]>>5
            isolated = inject(original[:first_end], drc()) + original[first_end:]
            result = decode(isolated, cut=127)
            flags(result[:1], True)
            flags(result[1:], False)
            assert [p for _, p in result[3:]] == [p for _, p in reference[3:]]
            print('frame flags and absent-frame unity: PASS')
        if selected('routing'):
            assert decode(original, cut=127, boost=127) == reference
            flags(reference, False)
            for sign, cut, boost in ((1,0,127), (0,127,0)):
                result = decode(inject(original, drc(sign)), cut, boost)
                assert [p for _, p in result] == [p for _, p in reference]
                flags(result, False, True)
            print('compress/boost routing: PASS')
        if selected('excluded') and channels >= 2:
            result = decode(inject(original, drc(exclude=True)), 127)
            def right_bytes(records):
                return b''.join(p[i:i+4] for _,p in records for i in range(4, len(p), 8))
            assert right_bytes(result) == right_bytes(reference)
            ratio(samples(result, 0), samples(reference, 0), -20*math.log10(2) if enabled else 0)
            print('excluded channels: PASS')
        if selected('order'):
            assert decode(inject(original, drc(), False), 127) == decode(inject(original, drc()), 127)
            print('fill order: PASS')
        if selected('normalisation'):
            result = decode(inject(original, drc(ctl=0, ref=124)), target=92)
            ratio(samples(result), samples(reference), 20*math.log10(2)*32/24 if enabled else 0)
            assert all(r[4] == enabled and (not enabled or r[5] == 124) for r, _ in result)
            flags(result, True)
            assert [p for _, p in decode(original, target=92)] == [p for _, p in reference]
            decode(inject(original, drc(ctl=0, ref=124)), target=92, flush=5)
            packets = []
            offset = 0
            while offset < len(original):
                n = ((original[offset+3]&3)<<11) | original[offset+4]<<3 | original[offset+5]>>5
                packets.append(original[offset:offset+n])
                offset += n
            persistent = inject(packets[0], drc(ctl=0, ref=124)) + b''.join(packets[1:])
            result = decode(persistent, target=92)
            ratio(samples(result), samples(reference), 20*math.log10(2)*32/24 if enabled else 0)
            cleared = decode(persistent, target=92, flush=5)
            assert all(not r[4] for r, _ in cleared[5:])
            midpoint = len(packets)//2
            changing = b''.join(inject(p, drc(ctl=0, ref=124 if i < midpoint else 92)) for i,p in enumerate(packets))
            ramped = decode(changing, target=92)
            previous = 1.0
            for index, ((_, actual), (_, baseline)) in enumerate(zip(ramped, reference)):
                a = struct.unpack('='+'f'*(len(actual)//4), actual)
                b = struct.unpack('='+'f'*(len(baseline)//4), baseline)
                target_gain = 2**(32/24) if index < midpoint and enabled else 1.0
                for i, (x,y) in enumerate(zip(a,b)):
                    gain = previous + (target_gain-previous)*(i//channels+1)/(len(a)//channels)
                    assert abs(x-y*gain) < 2e-7, (index, i, x, y*gain)
                previous = target_gain
            print('normalisation, metadata and flush: PASS')
        if selected('malformed'):
            for payload in ('10110000', '10111000', drc(tops=(40,40), controls=(24,24)), ''):
                result = decode(inject(original, payload), 127)
                assert [p for _, p in result] == [p for _, p in reference]
                flags(result, False)
            for payload, count in ((drc(tops=(40,80), controls=(24,24))[:8], 1),):
                # A truncated band list must not consume audio syntax.
                result = decode(inject(original, payload, before=False, count=count), 127)
                assert [p for _, p in result] == [p for _, p in reference], (payload, count)
            print('malformed payload and zero count: PASS')
        if selected('validation'):
            for cut, boost, target in ((128,0,0),(0,128,0),(0,0,1),(0,0,39),(0,0,128)):
                result = subprocess.run([probe, aac, str(cut), str(boost), str(target), '1', '0'], capture_output=True)
                assert result.returncode == (3 if enabled else 0)
            print('config validation: PASS')
        if selected('multiband'):
            tones = (440, 4000, 10000)
            with wave.open(src, 'wb') as w:
                w.setparams((channels, 2, 44100, 0, 'NONE', ''))
                w.writeframes(b''.join(struct.pack('<' + 'h'*channels, *([int(sum(1500*math.sin(2*math.pi*f*i/44100) for f in tones))]*channels)) for i in range(22050)))
            os.unlink(aac)
            subprocess.run([faac, '-a', '--shortctl', '1', '--object-type', 'lc', '-o', aac, src], check=True, capture_output=True)
            multitone = open(aac, 'rb').read()
            base = samples(decode(multitone), 0)
            changed = samples(decode(inject(multitone, drc(tops=(31,95), controls=(24,12))), 127), 0)
            def energy(v, frequency):
                # Projection rejects leakage from the other two widely separated tones.
                return sum(x*math.cos(2*math.pi*frequency*i/44100) for i,x in enumerate(v))**2 + sum(x*math.sin(2*math.pi*frequency*i/44100) for i,x in enumerate(v))**2
            for frequency, gain in zip(tones, (.5, 2**(-.5), 1)):
                db = 10*math.log10(energy(changed, frequency)/energy(base, frequency))
                assert abs(db - (20*math.log10(gain) if enabled else 0)) < .1, (frequency, db)
            print('multiband and above-top unity: PASS')
            assert all(not r[6] for r, _ in decode(multitone)[3:]), 'spectral fixture must use long windows'
        if selected('limiter') and enabled:
            with wave.open(src, 'wb') as w:
                w.setparams((channels, 2, 44100, 0, 'NONE', ''))
                w.writeframes(b''.join(struct.pack('<' + 'h'*channels, *([int(30000*math.sin(2*math.pi*500*i/44100))]*channels)) for i in range(22050)))
            os.unlink(aac)
            subprocess.run([faac, '-a', '--object-type', 'lc', '-o', aac, src], check=True, capture_output=True)
            loud = inject(open(aac, 'rb').read(), drc(sign=0))
            # Float peaks prove the integer path must compress rather than merely stay quiet.
            assert max(abs(x) for x in samples(decode(loud, boost=127))) > 1.0
            data = b''.join(p for _, p in decode(loud, boost=127, floating=False)[3:])
            pcm = struct.unpack('=' + 'h' * (len(data) // 2), data)
            assert min(pcm) > -32768 and max(pcm) < 32767, (min(pcm), max(pcm))
            assert max(abs(x) for x in pcm) > .89*32768
            print('integer soft-knee limiter: PASS')
        if selected('hetilt') and sbr == 'true':
            # A longer projection reduces SBR startup history's effect on the HF level.
            # Keep the HF tone above the 11.025 kHz core but below
            # the less stable top SBR bands.
            tones = (500, 12000)
            with wave.open(src, 'wb') as w:
                w.setparams((channels, 2, 44100, 0, 'NONE', ''))
                w.writeframes(b''.join(struct.pack('<' + 'h'*channels, *([int(sum(6000*math.sin(2*math.pi*f*i/44100) for f in tones))]*channels)) for i in range(88200)))
            os.unlink(aac)
            subprocess.run([faac, '-a', '--object-type', 'he-aac-v1', '-b', str(32 * channels), '-o', aac, src], check=True, capture_output=True)
            he = open(aac, 'rb').read()
            base = samples(decode(he), 0)
            changed = samples(decode(inject(he, drc()), cut=127), 0)
            def energy(v, frequency):
                # Separate core and reconstructed HF levels so core energy cannot hide missing SBR gain.
                return sum(x*math.cos(2*math.pi*frequency*i/44100) for i,x in enumerate(v))**2 + sum(x*math.sin(2*math.pi*frequency*i/44100) for i,x in enumerate(v))**2
            for frequency in tones:
                assert energy(base, frequency) > 1e-8
                db = 10*math.log10(energy(changed, frequency)/energy(base, frequency))
                assert abs(db - (-20*math.log10(2) if enabled else 0)) < .5, (frequency, db)
            print('HE core and HF gain: PASS')
        if selected('short'):
            # An isolated burst forces the encoder's transient detector into short blocks.
            with wave.open(src, 'wb') as w:
                w.setparams((channels, 2, 44100, 0, 'NONE', ''))
                w.writeframes(b''.join(struct.pack('<' + 'h'*channels, *([int(6000*math.sin(i*1.7)) if i % 4096 < 64 else 0]*channels)) for i in range(22050)))
            os.unlink(aac)
            subprocess.run([faac, '-a', '--object-type', 'lc', '-o', aac, src], check=True, capture_output=True)
            transient = open(aac, 'rb').read()
            base_short = decode(transient)
            assert any(r[6] for r, _ in base_short), 'fixture must contain short windows'
            ratio(samples(decode(inject(transient, drc()), 127)), samples(base_short), -20*math.log10(2) if enabled else 0)
            print('verified short-window single-band gain: PASS')
        if selected('he') and sbr == 'true':
            os.unlink(aac)
            subprocess.run([faac, '-a', '--object-type', 'he-aac-v1', '-b', str(32 * channels), '-o', aac, src], check=True, capture_output=True)
            he = open(aac, 'rb').read()
            # SBR envelope reconstruction and startup history permit a looser level bound.
            ratio(samples(decode(inject(he, drc()), 127)), samples(decode(he)), -20*math.log10(2) if enabled else 0, .5)
            print('HE full-band gain: PASS')


if __name__ == '__main__':
    main()
