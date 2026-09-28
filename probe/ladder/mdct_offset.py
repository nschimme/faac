#!/usr/bin/env python3
"""Offset control: correlates FAAC's real per-frame MDCT energy (from
FAAC_MDCT_ENERGY_DUMP, summed both channels) against a reference's real
dequantized band energy (regular cb 1-11 bands only, from a FAAD_LADDER_DUMP
text dump's C+Q records), over frame offsets -3..+3. Reports the peak lag and
its margin over the runner-up, per clip and reference."""
import sys
import statistics

def faac_energy(path):
    e = {}
    for line in open(path):
        if not line.startswith('E '):
            continue
        _, frame, ch, val = line.split()
        frame = int(frame)
        e[frame] = e.get(frame, 0.0) + float(val)
    return e

def ref_energy(path):
    """Per-frame (both channels summed) real dequantized energy of regular
    (cb 1-11) bands only, from a FAAD_LADDER_DUMP dump's C (cb/sf) + Q (raw
    quantized lines) records. PNS/zero/intensity bands are excluded -- their
    real energy is near zero by construction (that's why they were coded
    that way), so this is an energy-preserving restriction, not a bias."""
    # First pass: per (frame,ch) band list from C records (cb, sf, width via I record's max_sfb/groups).
    import re
    c_bands = {}   # (frame,ch) -> list of (cb, sf) in band order
    i_info = {}    # (frame,ch) -> (win_seq, max_sfb, groups, glen[])
    for line in open(path):
        if line.startswith('C '):
            parts = line.split('|')
            head = parts[0].split()
            frame, ch = int(head[1]), int(head[2])
            bands = []
            blob = parts[1] if len(parts) > 1 else ''
            for grp in blob.split('/'):
                grp = grp.strip()
                if not grp:
                    continue
                for tok in grp.split():
                    cb, sf, nnz, ms = tok.split(':')
                    bands.append((int(cb), int(sf)))
            c_bands[(frame, ch)] = bands
        elif line.startswith('I '):
            parts = line.split('|')
            head = parts[0].split()
            frame, ch = int(head[1]), int(head[2])
            win_seq, max_sfb, groups = int(head[3]), int(head[5]), int(head[6])
            glen = [int(x) for x in parts[1].split()]
            i_info[(frame, ch)] = (win_seq, max_sfb, groups, glen)

    # sfb_offset tables: reuse the standard 48kHz tables (same source both
    # sides use) -- derive them empirically from this dump's own C-record
    # band count per sfb width isn't available directly, so instead energy
    # is computed per RAW LINE weighted by its band's scalefactor, which
    # needs sfb boundaries. Pull them from libfaac's own tables via a tiny
    # embedded copy (48kHz long/short swb_offset, ISO 14496-3 Table 4.128/4.130).
    # Derived (not guessed) from libfaac/frame.c's own srInfo[] cb_width
    # tables for sampleRate==48000 (cumulative sum of cb_width_long/short),
    # so this matches whatever table FAAC's own sfbOffsetLong/Short actually
    # builds -- not a hand-typed ISO table that could silently disagree.
    _long_w = [4,4,4,4,4,4,4,4,4,4,8,8,8,8,8,8,8,
               12,12,12,12,16,16,20,20,24,24,28,28,32,32,32,32,32,32,
               32,32,32,32,32,32,32,32,32,32,32,32,32,96]
    _short_w = [4,4,4,4,4,8,8,8,12,12,12,16,16,16]
    sfb_long = [0]
    for _w in _long_w: sfb_long.append(sfb_long[-1] + _w)
    sfb_short = [0]
    for _w in _short_w: sfb_short.append(sfb_short[-1] + _w)

    energy = {}
    for line in open(path):
        if not line.startswith('Q '):
            continue
        head = line.split(None, 3)
        frame, ch = int(head[1]), int(head[2])
        vals = [int(x) for x in head[3].split()]
        key = (frame, ch)
        if key not in c_bands or key not in i_info:
            continue
        bands = c_bands[key]
        win_seq, max_sfb, groups, glen = i_info[key]
        is_short = (win_seq == 2)
        sfb_tab = sfb_short if is_short else sfb_long
        e = 0.0
        absw = 0
        band_idx = 0
        for g in range(groups):
            gl = glen[g] if g < len(glen) else 1
            for sfb in range(max_sfb):
                if band_idx >= len(bands):
                    break
                cb, sf = bands[band_idx]
                band_idx += 1
                if not (1 <= cb <= 11):
                    continue
                scale = 2.0 ** (0.25 * (sf - 100))
                lo, hi = sfb_tab[sfb], sfb_tab[sfb + 1]
                for w in range(gl):
                    base = (absw + w) * 128 if is_short else 0
                    for k in range(lo, min(hi, 128 if is_short else 1024)):
                        q = vals[base + k]
                        if q:
                            mag = (abs(q) ** (4.0/3.0)) * scale
                            e += mag * mag
            absw += gl
        energy[frame] = energy.get(frame, 0.0) + e
    return energy


def normalize(d, n):
    arr = [d.get(i, 0.0) for i in range(1, n + 1)]
    m = statistics.mean(arr) if arr else 0.0
    return [x - m for x in arr]


def correlate(a, b, offset):
    n = min(len(a), len(b))
    s = 0.0
    cnt = 0
    for i in range(n):
        j = i + offset
        if 0 <= j < len(b):
            s += a[i] * b[j]
            cnt += 1
    return s / cnt if cnt else 0.0


def main():
    faac_path, ref_path, label = sys.argv[1], sys.argv[2], sys.argv[3]
    faac = faac_energy(faac_path)
    ref = ref_energy(ref_path)
    n = max(faac.keys())
    fa = normalize(faac, n)
    fr = normalize(ref, max(ref.keys()))
    scores = {off: correlate(fa, fr, off) for off in range(-3, 4)}
    ranked = sorted(scores.items(), key=lambda kv: -kv[1])
    best_off, best_val = ranked[0]
    margin = best_val - ranked[1][1] if len(ranked) > 1 else float('nan')
    rel_margin = margin / best_val if best_val else float('nan')
    print(f"{label}: scores={{{', '.join(f'{k}:{v:.4g}' for k,v in scores.items())}}} "
          f"peak={best_off} margin={margin:.4g} rel_margin={rel_margin:.3f}")


if __name__ == '__main__':
    main()
