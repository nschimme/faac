#!/usr/bin/env python3
"""Builds a line-substitution hybrid between step1's output and a reference
(fdk), for the decisive attribution: keep the reference's own scalefactors,
sections (cb) and grouping exactly, and swap in a chosen subset of step1's
per-line quantized values for the reference's own, per ARM. Output is the
same ReemitICS binary intermediate reemit_tool already consumes -- the
merge logic lives entirely in Python; the C side just writes whatever
(cb, sf, quantized[]) it's handed, through the real writer/huffbook (which
now recomputes book[] from the actual quantized values, not a preset cb --
required for this: a hybrid's per-band maxq is not guaranteed to be what
the reference's own cb was sized for).

Frame correspondence: step1's own decoded output frame N (1-based, as
FAAD_LADDER_DUMP numbers it) carries the reference's frame N+1's decisions
(FAAC_STEP1_OFFSET=1 in ciFrame-space, which is 0-based and one less than
FAAD3's 1-based frame counter). So reference frame R <-> step1 frame R-1.

Arms (rule applied per spectral line, band-major; sf/cb/grouping/window
always the reference's own, per band, regardless of arm):
  K0   : q = fdk's own q everywhere (must equal fdk's own stream)
  K1   : q = step1's own q everywhere (must equal step1, unchanged)
  Z    : q = step1's, but 0 wherever fdk's q is 0
  S    : q = step1's, but fdk's q wherever step1's q is 0 and fdk's isn't
  ZS   : q = 0 if fdk==0, else (fdk if step1==0, else step1)
  M    : q = fdk's wherever BOTH step1 and fdk are nonzero, else step1's
  LO   : q = fdk's below 2kHz, else step1's
  HI   : q = fdk's at/above 2kHz, else step1's
"""
import sys
import importlib.util


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


HERE = __file__.rsplit('/', 1)[0]
pd = _load('parse_dump', HERE + '/parse_dump.py')

REEMIT_MAX_WINDOWS = pd.REEMIT_MAX_WINDOWS
FRAME_LEN = pd.FRAME_LEN

long_w = [4,4,4,4,4,4,4,4,4,4,8,8,8,8,8,8,8,
          12,12,12,12,16,16,20,20,24,24,28,28,32,32,32,32,32,32,
          32,32,32,32,32,32,32,32,32,32,32,32,32,96]
short_w = [4,4,4,4,4,8,8,8,12,12,12,16,16,16]
long_off = [0]
for _w in long_w: long_off.append(long_off[-1] + _w)
short_off = [0]
for _w in short_w: short_off.append(short_off[-1] + _w)


def band_major_positions(ics):
    """Yields (base_plus_k, freq_hz, band_idx) for every line of every
    band, in the exact band-major order the writer/huffbook walk -- same
    walk as reemit.c's qs[] build and line_level.py's per_line()."""
    is_short = (ics.win_seq == 2)
    tab = short_off if is_short else long_off
    win_size = 128 if is_short else 1024
    absw = 0
    for g in range(ics.num_groups):
        gl = ics.group_len[g] if g < len(ics.group_len) else 1
        for sfb in range(ics.max_sfb):
            band = g * ics.max_sfb + sfb
            lo, hi = tab[sfb], tab[sfb + 1]
            freq = ((lo + hi) / 2) * (48000 / 2) / win_size
            for w in range(gl):
                base = (absw + w) * 128 if is_short else 0
                for k in range(lo, min(hi, win_size)):
                    yield (base + k, freq, band)
        absw += gl


def merge_quantized(arm, ref_ics, step1_ics):
    """Returns a new quantized[] (length FRAME_LEN) for ref_ics's band
    layout, merging step1_ics's and ref_ics's own raw quantized arrays
    (both window-major, same positions -- step1's window/grouping is
    forced from ref_ics's, so no reordering is needed)."""
    out = [0] * FRAME_LEN
    changed = 0
    total = 0
    for pos, freq, band in band_major_positions(ref_ics):
        cb = ref_ics.band_cb[band]
        if not (1 <= cb <= 11):
            continue  # zero/PNS/IS bands carry no lines to merge either way
        total += 1
        qf = ref_ics.quantized[pos]
        qs = step1_ics.quantized[pos]
        if arm == 'K0':
            qv = qf
        elif arm == 'K1':
            qv = qs
        elif arm == 'Z':
            qv = 0 if qf == 0 else qs
        elif arm == 'S':
            qv = qf if (qs == 0 and qf != 0) else qs
        elif arm == 'ZS':
            qv = 0 if qf == 0 else (qf if qs == 0 else qs)
        elif arm == 'M':
            qv = qf if (qs != 0 and qf != 0) else qs
        elif arm == 'LO':
            qv = qf if freq < 2000 else qs
        elif arm == 'HI':
            qv = qf if freq >= 2000 else qs
        else:
            raise ValueError(f'unknown arm {arm}')
        out[pos] = qv
        if qv != qs:
            changed += 1
    return out, changed, total


def main():
    if len(sys.argv) != 5:
        print("usage: hybrid_merge.py <arm> <step1_dump> <ref_dump> <out.bin>", file=sys.stderr)
        sys.exit(1)
    arm, step1_path, ref_path, out_path = sys.argv[1:5]

    step1_frames = pd.parse(step1_path)
    ref_frames = pd.parse(ref_path)
    max_ref_frame = max(ref_frames.keys())

    empty = pd.ICS()
    empty_packed = empty.pack()
    n_written = 0
    total_changed = total_lines = 0
    fallback_ics = 0
    with open(out_path, 'wb') as out:
        for rf in range(1, max_ref_frame + 1):
            sf = rf - 1  # step1's own decoded frame number for this reference frame
            ref_chs = ref_frames.get(rf, {})
            step1_chs = step1_frames.get(sf, {})
            for ch in (0, 1):
                ref_ics = ref_chs.get(ch)
                if ref_ics is None:
                    out.write(empty_packed)
                    continue
                step1_ics = step1_chs.get(ch)
                if step1_ics is None:
                    # No step1 counterpart for this one reference frame
                    # (only the very first: step1's own decoded numbering
                    # starts one frame later). Falling back to K0 (fdk's
                    # own q) for just this frame keeps a REAL AAC frame
                    # written for every reference frame -- reemit_tool
                    # skips writing anything for an all-absent record,
                    # which would shift every later frame's decoded frame
                    # number by one relative to the reference, corrupting
                    # every subsequent comparison silently.
                    step1_ics = ref_ics
                elif (step1_ics.win_seq != ref_ics.win_seq
                      or step1_ics.max_sfb != ref_ics.max_sfb
                      or step1_ics.num_groups != ref_ics.num_groups
                      or step1_ics.group_len != ref_ics.group_len):
                    # step1's own known gap: a transition window
                    # (LONG_SHORT/SHORT_LONG) is excluded from forcing (see
                    # LADDER_RESULT.md), so that one frame falls back to
                    # FAAC's natural encode, whose window/grouping and band
                    # layout won't generally match the reference's -- the
                    # band-major position-for-position assumption below
                    # requires matching layouts. Falls back to K0 (fdk's
                    # own value) for this one frame, same as the missing-
                    # counterpart case above, rather than silently indexing
                    # step1_ics.quantized[] at positions that mean a
                    # different band in step1's own (different) layout.
                    fallback_ics += 1
                    if arm == 'K1':
                        # Preserve K1's exact step1 known answer on a
                        # transition whose ICS layout cannot be merged.
                        ref_ics = step1_ics
                    else:
                        step1_ics = ref_ics
                merged, changed, total = merge_quantized(arm, ref_ics, step1_ics)
                total_changed += changed
                total_lines += total
                new_ics = pd.ICS()
                new_ics.present = 1
                new_ics.win_seq = ref_ics.win_seq
                new_ics.window_shape = ref_ics.window_shape
                new_ics.max_sfb = ref_ics.max_sfb
                new_ics.num_groups = ref_ics.num_groups
                new_ics.group_len = list(ref_ics.group_len)
                new_ics.global_gain = ref_ics.global_gain
                new_ics.num_bands = ref_ics.num_bands
                new_ics.band_cb = list(ref_ics.band_cb)
                new_ics.band_sf = list(ref_ics.band_sf)
                new_ics.band_ms = list(ref_ics.band_ms)
                new_ics.quantized = merged
                new_ics.tns_present = ref_ics.tns_present
                new_ics.tns_num_filt = list(ref_ics.tns_num_filt)
                new_ics.tns_coef_res = list(ref_ics.tns_coef_res)
                new_ics.tns_length = [list(x) for x in ref_ics.tns_length]
                new_ics.tns_order = [list(x) for x in ref_ics.tns_order]
                new_ics.tns_direction = [list(x) for x in ref_ics.tns_direction]
                new_ics.tns_compress = [list(x) for x in ref_ics.tns_compress]
                new_ics.tns_coef = [[list(f) for f in w] for w in ref_ics.tns_coef]
                out.write(new_ics.pack())
            n_written += 1
    print(f"arm={arm}: wrote {n_written} frames; lines changed (vs step1) "
          f"{total_changed}/{total_lines} ({100*total_changed/total_lines:.1f}%); "
          f"layout fallback ICS={fallback_ics}")


if __name__ == '__main__':
    main()
