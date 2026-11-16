# S12: libfaad decision dump

Decoder-only instrumentation of the in-tree libfaad (origin/master 3a656f7) that logs
encoder decisions of any AAC-LC / HE-AAC v1 / v2 stream (.m4a or ADTS). No behaviour
change when `FAAD_DECDUMP` is unset (PCM md5 identical, validated below).

## Build
    git -C /home/user/faac worktree add /home/user/work/faad-dump origin/master
    git -C /home/user/work/faad-dump apply /home/user/faac/probe/ladder/scripts/s12/libfaad_decdump.patch
    cd /home/user/work/faad-dump && meson setup build-rel -Dbuildtype=release && ninja -C build-rel
    # binary: build-rel/frontend/faad (reads .m4a and ADTS directly)
    # HE-AAC v2 test encodes need: meson setup build-ps -Dencoder-ps=true

## Usage
    decdump.sh <stream.m4a|.aac> <dumpfile>     # DECDUMP_BIN overrides the faad path
    decstats.py [--json o.json] [--md o.md] [--seq-dir DIR] dump...
Or directly: `FAAD_DECDUMP=out.txt faad -w -f raw in.m4a >/dev/null`. The file is
truncated on open. Frame = raw_data_block index (decode call), from 0.

## Record format (space separated, one per line)
- `R <core_rate> <frame_len> <core_channels>` once, frame 0 (core rate; half of output rate with SBR).
- `F <frame> <SCE|CPE|LFE> <common_window> <ms_mask_present>` per element (ms_mask_present 0 none, 1 explicit, 2 all).
- `I <frame> <ch> <window_seq> <window_shape> <max_sfb> <num_groups> <group_lens,> <global_gain> <tns_present> <tns_nfilt per window,> <tns o/l summary> <pulse> <gain_control>`
  window_seq 0 ONLY_LONG, 1 LONG_START, 2 EIGHT_SHORT, 3 LONG_STOP. TNS summary `w<win>:o<order>/l<len>` joined by `+`, `-` if none. nfilt lists 8 windows for short, 1 for long.
- `B <frame> <ch> <group> <sfb> <start_line> <end_line> <codebook> <sf|is_pos|noise_energy> <ms_used>` per band below max_sfb. Codebook 0 zero, 1-11 spectral, 13 PNS, 14/15 intensity. ms_used is the transmitted mask (all-ones mask shown as 1), recorded before PNS/MS/IS processing. Lines are per window for short blocks. Written before dequant-side clearing.
- `S <frame> <ch> <bs_amp_res> <start_freq> <stop_freq> <xover_band> <freq_scale> <num_env> <frame_class> <num_noise> <invf,> <add_harmonic_flag> <coupling>` per SBR channel per fill (header fields are the current header).
- `P <frame> <enable_iid> <enable_icc> <num_env> <iid_mode> <icc_mode>` per PS payload (mode -1 when disabled; iid_mode 0-2 coarse, 3-5 fine).
This is independent of the older `FAAD_DUMP` / `-Dstats` records.

## decstats.py
Per stream and aggregate (JSON + markdown). Per CPE pair band, classification priority:
IS (either channel cb 14/15) > PNS (either 13) > zero (both 0) > M/S (mask) > L/R. SCE bands are
reported separately (mono spec/PNS/zero). Regions use the band centre at the core rate
(0-2, 2-4, 4-6, 6-8, 8-12, 12-16, 16-24 kHz), split by long-type (ONLY_LONG/START/STOP) vs EIGHT_SHORT;
counts per band and per line. Also EIGHT_SHORT share per channel, TNS share/nfilt/order by window type,
`--seq-dir` per-stream CSV `frame,ch,window_seq,tns_present,nfilt`, SBR/PS distributions.
Note: with SBR the AAC core only spans to core_rate/2, so regions above that are empty.

## Validation (20 s-ish test excerpt, FAAC from the same worktree, 44.1 kHz)
- PCM md5 with vs without FAAD_DECDUMP identical on 6 streams (Apple LC 128k, Apple HE 64k, FAAC `--joint` 0/1/2/3 LC, FAAC he-v1, he-v2).
- `--joint 0` (lr): 339 frames, 11360 L/R bands, M/S = 0, IS = 0. `--joint 1` (ms): M/S 13786, IS = 0.
- `--joint 2` (is): IS 10296, M/S = 0. `--joint 3` (auto): M/S 8842, IS 4953.
- HE-AAC v2 (FAAC): S and P records present; HE v1 (Apple) S records present.
- m4a vs ffmpeg ADTS remux gives the same dump except the last frame ffmpeg/ADTS drops.
