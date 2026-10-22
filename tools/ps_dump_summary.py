#!/usr/bin/env python3
"""Summarize FAAD_DUMP PS and AAC core diagnostic records."""
import argparse
from collections import Counter
from pathlib import Path


def distribution(counter):
    return ", ".join(f"{key}={counter[key]}" for key in sorted(counter)) or "none"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dump", type=Path, help="text file written by FAAD_DUMP")
    args = parser.parse_args()
    ps_frames, phase_frames = set(), set()
    iid_modes, icc_modes = Counter(), Counter()
    env_counts, env_borders = Counter(), Counter()
    iid_values, icc_values, ipd_values, opd_values = Counter(), Counter(), Counter(), Counter()
    core_sequences, max_sfb = Counter(), Counter()
    sfb_by_sequence = {}
    bit_rows = []
    legacy_ps = 0
    with args.dump.open(encoding="utf-8", errors="replace") as stream:
        for line in stream:
            fields = line.split()
            if not fields:
                continue
            try:
                if fields[0] == "P" and len(fields) >= 5:
                    legacy_ps += 1
                elif fields[0] == "PM" and len(fields) == 9:
                    frame, iid, icc, phase, niid, nicc, nphase, frame_class = map(int, fields[1:])
                    ps_frames.add(frame)
                    phase_frames.update([frame] if phase else [])
                    iid_modes[iid] += 1
                    icc_modes[icc] += 1
                elif fields[0] == "PE" and len(fields) >= 4:
                    frame, env, border = map(int, fields[1:4])
                    parts = line.split("|")
                    if len(parts) == 4:
                        vectors = [parts[0].split()[4:], *(part.split() for part in parts[1:])]
                        for counter, values in zip((iid_values, icc_values, ipd_values, opd_values), vectors):
                            counter.update(map(int, values))
                        env_counts[frame] += 1
                        env_borders[border] += 1
                elif fields[0] == "C" and len(fields) >= 8:
                    sequence, sfb = int(fields[4]), int(fields[5])
                    core_sequences[sequence] += 1
                    max_sfb[sfb] += 1
                    sfb_by_sequence.setdefault(sequence, Counter())[sfb] += 1
                elif fields[0] == "B" and len(fields) == 11:
                    _, frame, total, hdr, sect, sf, spec, aux, sbr, ps, fill = fields
                    bit_rows.append(tuple(map(int, (total, hdr, sect, sf, spec, aux, sbr, ps, fill))))
            except (ValueError, IndexError):
                continue

    print(f"PS frames: {len(ps_frames)} (legacy P records: {legacy_ps})")
    if ps_frames:
        print(f"Phase enabled: {len(phase_frames)}/{len(ps_frames)} ({100 * len(phase_frames) / len(ps_frames):.1f}%)")
        print(f"IID modes: {distribution(iid_modes)}")
        print(f"ICC modes: {distribution(icc_modes)}")
        print(f"Envelopes per frame: {distribution(Counter(env_counts.values()))}")
        print(f"Envelope ending borders: {distribution(env_borders)}")
        print(f"IID values: {distribution(iid_values)}")
        print(f"ICC values: {distribution(icc_values)}")
        print(f"IPD values: {distribution(ipd_values)}")
        print(f"OPD values: {distribution(opd_values)}")
    if core_sequences:
        names = {0: "long", 1: "start", 2: "short", 3: "stop"}
        print("Core window sequences: " + distribution(Counter({names.get(k, str(k)): v for k, v in core_sequences.items()})))
        print(f"Core max_sfb: {distribution(max_sfb)}")
        for sequence, counts in sorted(sfb_by_sequence.items()):
            print(f"Core max_sfb ({names.get(sequence, sequence)}): {distribution(counts)}")
    if bit_rows:
        n = len(bit_rows)
        means = [sum(row[i] for row in bit_rows) / n for i in range(9)]
        total, hdr, sect, sf, spec, aux, sbr, ps, fill = means
        core = total - sbr - ps - fill
        print(f"Mean B bits/frame: total={total:.1f} core={core:.1f} SBR={sbr:.1f} PS={ps:.1f} fill={fill:.1f}")
        if total:
            print(f"Mean bit shares: core={100*core/total:.1f}% SBR={100*sbr/total:.1f}% PS={100*ps/total:.1f}% fill={100*fill/total:.1f}%")


if __name__ == "__main__":
    main()
