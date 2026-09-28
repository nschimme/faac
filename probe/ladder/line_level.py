import re, sys, math
from collections import defaultdict

def load_frames(path):
    """Returns dict[(frame,ch)] = dict with win_seq, max_sfb, groups, glen, cb[band], sf[band], global_gain, quantized[1024]"""
    frames = {}
    for line in open(path):
        if line.startswith('C '):
            parts = line.split('|')
            head = parts[0].split()
            frame, ch = int(head[1]), int(head[2])
            rec = frames.setdefault((frame,ch), {})
            bands = []
            for grp in (parts[1] if len(parts)>1 else '').split('/'):
                grp = grp.strip()
                if not grp: continue
                for tok in grp.split():
                    cb, sf, nnz, ms = tok.split(':')
                    bands.append((int(cb), int(sf)))
            rec['bands'] = bands
        elif line.startswith('I '):
            parts = line.split('|')
            head = parts[0].split()
            frame, ch = int(head[1]), int(head[2])
            rec = frames.setdefault((frame,ch), {})
            rec['win_seq'] = int(head[3])
            rec['max_sfb'] = int(head[5])
            rec['groups'] = int(head[6])
            rec['global_gain'] = int(head[7])
            rec['glen'] = [int(x) for x in parts[1].split()]
        elif line.startswith('Q '):
            head = line.split(None, 3)
            frame, ch = int(head[1]), int(head[2])
            rec = frames.setdefault((frame,ch), {})
            rec['quantized'] = [int(x) for x in head[3].split()]
    return frames

long_w = [4,4,4,4,4,4,4,4,4,4,8,8,8,8,8,8,8,
           12,12,12,12,16,16,20,20,24,24,28,28,32,32,32,32,32,32,
           32,32,32,32,32,32,32,32,32,32,32,32,32,96]
short_w = [4,4,4,4,4,8,8,8,12,12,12,16,16,16]
long_off = [0]
for w in long_w: long_off.append(long_off[-1]+w)
short_off = [0]
for w in short_w: short_off.append(short_off[-1]+w)

def region(freq_hz):
    if freq_hz < 2000: return "0-2k"
    if freq_hz < 6000: return "2-6k"
    if freq_hz < 12000: return "6-12k"
    return ">12k"

def qbucket(q):
    a = abs(q)
    if a == 0: return "0"
    if a == 1: return "1"
    if a <= 4: return "2-4"
    return ">4"

def per_line(rec, sample_rate=48000):
    """Yields (freq_hz, q, sf, cb) for every regular-band line, band-major."""
    win_seq = rec['win_seq']
    is_short = (win_seq == 2)
    tab = short_off if is_short else long_off
    max_sfb = rec['max_sfb']
    glen = rec['glen']
    bands = rec['bands']
    quantized = rec['quantized']
    win_size = 128 if is_short else 1024
    absw = 0
    bi = 0
    for g in range(rec['groups']):
        gl = glen[g] if g < len(glen) else 1
        for sfb in range(max_sfb):
            if bi >= len(bands): break
            cb, sf = bands[bi]; bi += 1
            lo, hi = tab[sfb], tab[sfb+1]
            for w in range(gl):
                base = (absw+w)*128 if is_short else 0
                for k in range(lo, min(hi, win_size)):
                    q = quantized[base+k]
                    freq = k * (sample_rate/2) / win_size
                    yield (freq, q, sf, cb, is_short)
        absw += gl

def main():
    step1_path, ref_path, offset = sys.argv[1], sys.argv[2], int(sys.argv[3])
    step1 = load_frames(step1_path)
    ref = load_frames(ref_path)
    max_frame = max(f for f,c in step1.keys())

    match_table = defaultdict(lambda: defaultdict(int))  # region -> bucket -> [count, match0, match1, matchbig]
    counts = defaultdict(lambda: defaultdict(lambda: [0,0,0,0]))  # region,bucket -> [total, exact, off1, biggerdiff]
    ratio_samples = defaultdict(list)

    for frame in range(1, max_frame+1):
        ref_frame = frame + offset
        for ch in (0,1):
            s = step1.get((frame,ch))
            r = ref.get((ref_frame,ch))
            if not s or not r or 'quantized' not in s or 'quantized' not in r:
                continue
            if s.get('win_seq') != r.get('win_seq') or s.get('max_sfb') != r.get('max_sfb'):
                continue
            if s.get('groups') != r.get('groups') or s.get('glen') != r.get('glen'):
                # Grouping should always match (step1 forces it from the
                # reference), but skip rather than silently misalign a
                # band-major walk if it somehow doesn't.
                continue
            s_lines = list(per_line(s))
            r_lines = list(per_line(r))
            n = min(len(s_lines), len(r_lines))
            for i in range(n):
                fs, qs_, sfs, cbs, is_short = s_lines[i]
                fr, qr, sfr, cbr, _ = r_lines[i]
                reg = region(fs)
                bucket = qbucket(qr)
                d = abs(qs_ - qr)
                c = counts[reg][bucket]
                c[0] += 1
                if d == 0: c[1] += 1
                elif d == 1: c[2] += 1
                else: c[3] += 1
                # Signed dequantized ratio: sf is forced equal between step1
                # and the reference for a matching band, so this reduces to
                # sign(qs)*|qs|^(4/3) / (sign(qr)*|qr|^(4/3)) -- a PREVIOUS
                # version of this script used unsigned |q| here, which made
                # an opposite-sign, equal-magnitude line register as a
                # perfect ratio=1.0 match while the line-level exact-match
                # table (which is sign-aware) correctly counted it as a big
                # miss. That mismatch between the two tables was exactly the
                # inconsistency flagged in review; keeping sign here is the
                # fix, not a stylistic change.
                if qs_ != 0 and qr != 0:
                    scale_s = 2.0**(0.25*(sfs-100))
                    scale_r = 2.0**(0.25*(sfr-100))
                    mag_s = math.copysign(abs(qs_)**(4.0/3)*scale_s, qs_)
                    mag_r = math.copysign(abs(qr)**(4.0/3)*scale_r, qr)
                    if mag_r != 0:
                        ratio_samples[reg].append(mag_s/mag_r)

    print("Line-level match, split by band region and reference |q|:")
    print(f"{'region':<8}{'|q| bucket':<12}{'total':>8}{'exact %':>10}{'off1 %':>9}{'bigger %':>10}")
    for reg in ["0-2k","2-6k","6-12k",">12k"]:
        for bucket in ["0","1","2-4",">4"]:
            c = counts[reg][bucket]
            if c[0] == 0: continue
            print(f"{reg:<8}{bucket:<12}{c[0]:>8}{100*c[1]/c[0]:>9.1f}%{100*c[2]/c[0]:>8.1f}%{100*c[3]/c[0]:>9.1f}%")

    print()
    print("MDCT-magnitude ratio (step1 dequant / reference dequant), median and IQR, per region (nonzero-nonzero lines only):")
    for reg in ["0-2k","2-6k","6-12k",">12k"]:
        vals = sorted(ratio_samples[reg])
        if not vals: continue
        n = len(vals)
        med = vals[n//2]
        q1 = vals[n//4]
        q3 = vals[3*n//4]
        print(f"{reg:<8} n={n:<8} median={med:.3f}  IQR=[{q1:.3f}, {q3:.3f}]")

if __name__ == '__main__':
    main()
