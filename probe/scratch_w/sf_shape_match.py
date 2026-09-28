#!/usr/bin/env python3
"""Compares an injected output stream's own encoded per-band scalefactor
shape (its sf minus its own global_gain) against the donor's, at the
correlated frame, for every band whose donor class is a coded (1-11) band.
Usage: sf_shape_match.py <output.dump> <donor.dump> <shift>
shift: donor_frame = output_frame + shift.
"""
import sys

def parse(path):
    recs = {}
    for line in open(path):
        if not line.startswith('C '):
            continue
        parts = line.split(' | ')
        head = parts[0].split()
        frame, ch, gg = int(head[1]), int(head[2]), int(head[7])
        bar = parts[1].rsplit(' g=', 1)[0] if len(parts) > 1 else ''
        bands = []
        for grp in bar.split('/'):
            for tok in grp.split():
                f = tok.split(':')
                if len(f) == 4:
                    cb, sf, nnz, ms = map(int, f)
                    bands.append((cb, sf))
        recs[(frame, ch)] = (gg, bands)
    return recs

def main():
    out_path, donor_path, shift = sys.argv[1], sys.argv[2], int(sys.argv[3])
    out = parse(out_path)
    donor = parse(donor_path)

    matched = 0
    total = 0
    mismatches = []
    for (f, ch), (my_gg, my_bands) in out.items():
        key = (f + shift, ch)
        if key not in donor:
            continue
        don_gg, don_bands = donor[key]
        n = min(len(my_bands), len(don_bands))
        for b in range(n):
            don_cb, don_sf = don_bands[b]
            my_cb, my_sf = my_bands[b]
            # Shape-forcing only ever applies where BOTH sides naturally/
            # already ended up as a coded (1-11) band -- FAAC's own zero/PNS
            # decision isn't touched by `sf` alone (no `class` in this arm),
            # so a donor-coded band FAAC decided to zero is a natural class
            # disagreement, not a forcing failure.
            if don_cb < 1 or don_cb > 11 or my_cb < 1 or my_cb > 11:
                continue
            total += 1
            my_shape = my_sf - my_gg
            don_shape = don_sf - don_gg
            if my_shape == don_shape:
                matched += 1
            else:
                mismatches.append((f, ch, b, my_cb, my_shape, don_shape))
    pct = 100.0 * matched / total if total else float('nan')
    print(f"{matched}/{total} ({pct:.1f}%)")
    for m in mismatches[:8]:
        print("  mismatch", m)

if __name__ == '__main__':
    main()
