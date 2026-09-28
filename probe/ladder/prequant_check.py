#!/usr/bin/env python3
"""Pre-quantization spectrum check (ladder step 1). Compares FAAC's real
MDCT spectrum -- dumped by frame.c's FAAC_STEP1_SPEC_DUMP right after the
forced window/M-S/TNS stages, before Step1Quantize -- against a reference's
own dequantized values at the reference's own scalefactors, line for line.

If the spectra genuinely match (same audio, same window/M-S/TNS decisions),
the residual should look like fdk's own quantization noise: correlation near
1, ratio median near 1, and a residual well below the reference's own signal
level. A larger mismatch points at a real pipeline bug in window-shape
overlap handling (KBD), TNS direction/region, M/S, or frame alignment.

Usage: prequant_check.py <spec_dump> <reference_ladder_dump> <ciFrame-to-
reference-dump-frame offset, e.g. 2 for FAAC_STEP1_OFFSET=1>
"""
import sys
import math
from collections import defaultdict

sys.path.insert(0, __file__.rsplit('/', 1)[0])
from line_level import load_frames, per_line, region, short_off, long_off  # noqa: E402


def load_spec(path):
    specs = {}
    for line in open(path):
        if not line.startswith('S '):
            continue
        head = line.split(None, 3)
        ciframe, ch = int(head[1]), int(head[2])
        specs[(ciframe, ch)] = [float(x) for x in head[3].split()]
    return specs


def main():
    spec_path, ref_path, offset = sys.argv[1], sys.argv[2], int(sys.argv[3])
    specs = load_spec(spec_path)
    ref = load_frames(ref_path)

    # region -> lists
    faac_vals = defaultdict(list)
    ref_vals = defaultdict(list)
    ratios = defaultdict(list)
    residual_sq = defaultdict(float)
    ref_sq = defaultdict(float)
    n_lines = defaultdict(int)
    n_matched_frames = 0
    n_skipped_shape = 0
    ratios_by_q = defaultdict(list)

    max_ciframe = max(k[0] for k in specs.keys())
    for ciframe in range(0, max_ciframe + 1):
        ref_frame = ciframe + offset
        for ch in (0, 1):
            sp = specs.get((ciframe, ch))
            r = ref.get((ref_frame, ch))
            if sp is None or r is None or 'quantized' not in r:
                continue
            n_matched_frames += 1
            is_short = (r['win_seq'] == 2)
            win_size = 128 if is_short else 1024
            tab = short_off if is_short else long_off
            absw = 0
            bi = 0
            bands = r['bands']
            glen = r['glen']
            for g in range(r['groups']):
                gl = glen[g] if g < len(glen) else 1
                for sfb in range(r['max_sfb']):
                    if bi >= len(bands):
                        break
                    cb, sf = bands[bi]
                    bi += 1
                    lo, hi = tab[sfb], tab[sfb + 1]
                    freq = ((lo + hi) / 2) * (48000 / 2) / win_size
                    reg = region(freq)
                    for w in range(gl):
                        base = (absw + w) * 128 if is_short else 0
                        rq_base = base  # same layout in both quantized[] and spec[]
                        for k in range(lo, min(hi, win_size)):
                            qr = r['quantized'][rq_base + k]
                            if qr == 0:
                                continue  # only compare where the reference actually coded content
                            scale_r = 2.0 ** (0.25 * (sf - 100))
                            mag_r = math.copysign(abs(qr) ** (4.0 / 3) * scale_r, qr)
                            v_s = sp[base + k]
                            n_lines[reg] += 1
                            faac_vals[reg].append(v_s)
                            ref_vals[reg].append(mag_r)
                            resid = v_s - mag_r
                            residual_sq[reg] += resid * resid
                            ref_sq[reg] += mag_r * mag_r
                            if v_s != 0:
                                ratios[reg].append(v_s / mag_r)
                            aq = abs(qr)
                            qb = 'small(1-2)' if aq <= 2 else 'mid(3-8)' if aq <= 8 else 'large(>8)'
                            ratios_by_q[qb].append(v_s / mag_r if v_s != 0 else 0.0)
                absw += gl

    print(f"matched (ciframe,ch) pairs: {n_matched_frames}")
    print()
    print(f"{'region':<8}{'n':>10}{'corr':>8}{'ratio_med':>11}{'ratio_IQR':>22}{'resid_dB':>10}")
    for reg in ["0-2k", "2-6k", "6-12k", ">12k"]:
        n = n_lines[reg]
        if n == 0:
            continue
        fv = faac_vals[reg]
        rv = ref_vals[reg]
        mf = sum(fv) / n
        mr = sum(rv) / n
        cov = sum((a - mf) * (b - mr) for a, b in zip(fv, rv)) / n
        vf = sum((a - mf) ** 2 for a in fv) / n
        vr = sum((b - mr) ** 2 for b in rv) / n
        corr = cov / math.sqrt(vf * vr) if vf > 0 and vr > 0 else float('nan')
        rs = sorted(ratios[reg])
        nr = len(rs)
        med = rs[nr // 2] if nr else float('nan')
        q1 = rs[nr // 4] if nr else float('nan')
        q3 = rs[3 * nr // 4] if nr else float('nan')
        resid_db = 10 * math.log10(residual_sq[reg] / ref_sq[reg]) if ref_sq[reg] > 0 else float('nan')
        print(f"{reg:<8}{n:>10}{corr:>8.4f}{med:>11.3f}   [{q1:>6.3f},{q3:>6.3f}]{resid_db:>10.1f}")

    print()
    print("Ratio median/IQR by reference |q| magnitude (all regions pooled) -- distinguishes a real")
    print("scale/window mismatch (bias persists at large |q|) from inherent low-|q| AAC quantization")
    print("coarseness (bias only at small |q|):")
    for qb in ['small(1-2)', 'mid(3-8)', 'large(>8)']:
        rs = sorted(ratios_by_q[qb])
        nr = len(rs)
        if nr == 0:
            continue
        med = rs[nr // 2]
        q1 = rs[nr // 4]
        q3 = rs[3 * nr // 4]
        print(f"  {qb:<12} n={nr:<9} median={med:.3f}  IQR=[{q1:.3f}, {q3:.3f}]")


if __name__ == '__main__':
    main()
