#!/usr/bin/env python3
"""Parses a FAAD_LADDER_DUMP text dump (C/I/Q records, see libfaad/decoder.c
core_dump_ics) into the fixed-layout binary intermediate reemit_main.c reads.
Keeping the parsing in Python (easy to get right and to print/debug) and the
C side to a flat fread() keeps the ladder's real subject -- FAAC's bitstream
writer -- decoupled from a hand-rolled C text parser that could itself hide
bugs.

Wire format per ICS record, all little-endian int32, field order fixed and
independent of any C struct's padding (read explicitly by reemit_main.c, not
memcpy'd onto a struct):

present, win_seq, window_shape, max_sfb, num_groups, group_len[8],
global_gain, num_bands, band_cb[128], band_sf[128], band_ms[128],
quantized[1024], tns_present, tns_num_filt[8], tns_coef_res[8],
tns_length[8*3], tns_order[8*3], tns_direction[8*3], tns_compress[8*3],
tns_coef[8*3*20]

One frame = ch0 record followed by ch1 record (ch1.present=0 for a bare SCE).
Frames are emitted in increasing frame-number order; a frame with no C/I/Q
data for a channel (shouldn't happen for a CPE-only corpus) is skipped.
"""
import struct
import sys

MAX_SCFAC_BANDS = 128
REEMIT_MAX_WINDOWS = 8
TNS_MAX_FILTERS = 3
TNS_MAX_ORDER = 20
FRAME_LEN = 1024


class ICS:
    def __init__(self):
        self.present = 0
        self.win_seq = 0
        self.window_shape = 0
        self.max_sfb = 0
        self.num_groups = 0
        self.group_len = [0] * REEMIT_MAX_WINDOWS
        self.global_gain = 0
        self.num_bands = 0
        self.band_cb = [0] * MAX_SCFAC_BANDS
        self.band_sf = [0] * MAX_SCFAC_BANDS
        self.band_ms = [0] * MAX_SCFAC_BANDS
        self.quantized = [0] * FRAME_LEN
        self.tns_present = 0
        self.tns_num_filt = [0] * REEMIT_MAX_WINDOWS
        self.tns_coef_res = [0] * REEMIT_MAX_WINDOWS
        self.tns_length = [[0] * TNS_MAX_FILTERS for _ in range(REEMIT_MAX_WINDOWS)]
        self.tns_order = [[0] * TNS_MAX_FILTERS for _ in range(REEMIT_MAX_WINDOWS)]
        self.tns_direction = [[0] * TNS_MAX_FILTERS for _ in range(REEMIT_MAX_WINDOWS)]
        self.tns_compress = [[0] * TNS_MAX_FILTERS for _ in range(REEMIT_MAX_WINDOWS)]
        self.tns_coef = [[[0] * TNS_MAX_ORDER for _ in range(TNS_MAX_FILTERS)]
                         for _ in range(REEMIT_MAX_WINDOWS)]

    def pack(self):
        out = [self.present, self.win_seq, self.window_shape, self.max_sfb,
               self.num_groups] + self.group_len + [self.global_gain, self.num_bands]
        out += self.band_cb + self.band_sf + self.band_ms + self.quantized
        out += [self.tns_present] + self.tns_num_filt + self.tns_coef_res
        for w in range(REEMIT_MAX_WINDOWS):
            out += self.tns_length[w]
        for w in range(REEMIT_MAX_WINDOWS):
            out += self.tns_order[w]
        for w in range(REEMIT_MAX_WINDOWS):
            out += self.tns_direction[w]
        for w in range(REEMIT_MAX_WINDOWS):
            out += self.tns_compress[w]
        for w in range(REEMIT_MAX_WINDOWS):
            for f in range(TNS_MAX_FILTERS):
                out += self.tns_coef[w][f]
        return struct.pack('<%di' % len(out), *out)


def parse(path):
    frames = {}  # frame -> {ch: ICS}
    with open(path) as fh:
        cur_key = None
        for line in fh:
            line = line.rstrip('\n')
            if not line:
                continue
            tag = line[0]
            if tag == 'C':
                parts = line.split('|')
                head = parts[0].split()
                frame = int(head[1]); ch = int(head[2])
                # bits = head[3] unused; win_seq/max_sfb/groups/global_gain
                # come from the richer 'I' record instead (same values).
                ics = frames.setdefault(frame, {}).setdefault(ch, ICS())
                ics.present = 1
                bands = []
                bands_blob = parts[1] if len(parts) > 1 else ''
                for grp in bands_blob.split('/'):
                    grp = grp.strip()
                    if not grp:
                        continue
                    for tok in grp.split():
                        cb, sf, nnz, ms = tok.split(':')
                        bands.append((int(cb), int(sf), int(ms)))
                for i, (cb, sf, ms) in enumerate(bands):
                    if i >= MAX_SCFAC_BANDS:
                        break
                    ics.band_cb[i] = cb
                    ics.band_sf[i] = sf
                    ics.band_ms[i] = ms
                ics.num_bands = min(len(bands), MAX_SCFAC_BANDS)
            elif tag == 'I':
                parts = line.split('|')
                head = parts[0].split()
                frame = int(head[1]); ch = int(head[2])
                ics = frames.setdefault(frame, {}).setdefault(ch, ICS())
                ics.present = 1
                ics.win_seq = int(head[3])
                ics.window_shape = int(head[4])
                ics.max_sfb = int(head[5])
                ics.num_groups = int(head[6])
                ics.global_gain = int(head[7])
                # head[8] = pulse_data_present, head[9] = tns_data_present
                ics.tns_present = int(head[9])
                glens = parts[1].split()
                for i, g in enumerate(glens):
                    if i < REEMIT_MAX_WINDOWS:
                        ics.group_len[i] = int(g)
                # parts[2] = pulse_count pulse_start_sfb offset:amp...(ignored: never fires)
                # parts[3] = per-window TNS: "n:res[len,order,dir,cmp,c1,c2,...]..." repeated
                tsec = parts[3].strip() if len(parts) > 3 else ''
                w = 0
                i = 0
                toks = _split_tns(tsec)
                for w, tok in enumerate(toks):
                    if w >= REEMIT_MAX_WINDOWS:
                        break
                    nfilt, res, filters = tok
                    ics.tns_num_filt[w] = nfilt
                    ics.tns_coef_res[w] = res
                    for f, filt in enumerate(filters):
                        if f >= TNS_MAX_FILTERS:
                            break
                        length, order, direction, compress, coefs = filt
                        ics.tns_length[w][f] = length
                        ics.tns_order[w][f] = order
                        ics.tns_direction[w][f] = direction
                        ics.tns_compress[w][f] = compress
                        for k, c in enumerate(coefs):
                            if k < TNS_MAX_ORDER:
                                ics.tns_coef[w][f][k] = c
            elif tag == 'Q':
                head = line.split(None, 3)
                frame = int(head[1]); ch = int(head[2])
                vals = [int(x) for x in head[3].split()]
                ics = frames.setdefault(frame, {}).setdefault(ch, ICS())
                for i, v in enumerate(vals[:FRAME_LEN]):
                    ics.quantized[i] = v
    return frames


def _split_tns(tsec):
    """Parses ' n:res[len,order,dir,cmp,c...][len,...]... n:res ...' into a
    list of (nfilt, coef_res, [(length,order,direction,compress,[coefs]),...])
    per window, in window order."""
    import re
    out = []
    # Each window entry starts with "N:R" optionally followed by N bracketed filters.
    pos = 0
    for m in re.finditer(r'(\d+):(\d+)((?:\[[^\]]*\])*)', tsec):
        nfilt = int(m.group(1)); res = int(m.group(2))
        filters = []
        for fm in re.finditer(r'\[([^\]]*)\]', m.group(3)):
            nums = [int(x) for x in fm.group(1).split(',')]
            length, order, direction, compress = nums[0:4]
            coefs = nums[4:]
            filters.append((length, order, direction, compress, coefs))
        out.append((nfilt, res, filters))
    return out


def main():
    if len(sys.argv) != 3:
        print("usage: parse_dump.py <ladder.dump> <out.bin>", file=sys.stderr)
        sys.exit(1)
    frames = parse(sys.argv[1])
    max_frame = max(frames.keys()) if frames else 0
    empty = ICS()
    empty_packed = empty.pack()
    n_written = 0
    with open(sys.argv[2], 'wb') as out:
        for fr in range(1, max_frame + 1):
            chs = frames.get(fr, {})
            for ch in (0, 1):
                ics = chs.get(ch)
                out.write(ics.pack() if ics is not None else empty_packed)
            n_written += 1
    print(f"wrote {n_written} frames ({n_written*2} ICS records) to {sys.argv[2]}")


if __name__ == '__main__':
    main()
