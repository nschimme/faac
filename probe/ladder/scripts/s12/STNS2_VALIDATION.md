# S12: short-window TNS v2 (attack window only) scratch patch

Patch: `stns2_scratch.patch` (full `git diff origin/master`, base 3a656f7, worktree /home/user/work/stns2; includes the v1 patch). Prereg: `results/s12-apple48k/stage3-transients/PREREG_stns_v2.md`. Research only.

## v2
- New knob `FAAC_STNS_ATK=1`, read once in `stns_init` (same place as the other STNS knobs) into `TnsInfo.shortAtkOnly`; only meaningful with `FAAC_STNS=1`.
- In `TnsEncodeShort`: E[w] = sum of squares of the 128 lines of window w (`spec + w*128`, still contiguous at TNS time). Attack window = argmax over w=1..7 of E[w] / max(E[w-1], 1e-9) with ratio > 1. Only that window goes through the v1 fit/filter (same order/gain/start/stop); all others get n_filt 0. No window with ratio > 1 means no TNS in the frame.
- Choice for w=0: never a candidate. The previous frame's last-window energy is not kept in the coder state, and carrying it would add per-channel history across block-type switches; skipped to keep the patch small. Consequence: an attack in window 0 is not filtered.
- Cost is one 1024-line sum of squares per EIGHT_SHORT channel, before the fit.

## a. Env unset == master
20 encodes, ADTS output, `ffmpeg -map 0:a -c copy -f md5` of master (`faac-master/build-ps`) vs v2 (`stns2/build`, PS on). Clips 01 castanets, 07 applaud, 13 fatboy_30sec (full) x LC `-b 64`, LC `-b 64 --cbr`, LC `-q 100`, HE-v1 `-b 32`, HE-v1 `-b 48 --cbr`, HE-v2 `-b 32` (18), plus 24-Greensleeves LC `-b 128` and HE-v1 `-b 24 --cbr`.
**20 / 20 identical.**

## b. FAAC_STNS=1, no ATK == v1
v1 binary (`stns/build`, `FAAC_STNS=1`) vs v2 (`FAAC_STNS=1`, ATK unset): 3 clips x LC `-b 64`, LC `-b 32 --cbr` (6), plus 3 x HE-v1 `-b 48 --cbr`.
**9 / 9 identical.**

## c. FAAC_STNS=1 FAAC_STNS_ATK=1 decode cleanliness
Clips 13 fatboy_30sec, 01 castanets, bas, 24-Greensleeves; LC 32/64/128 and HE-v1 32/48, all `--cbr` (20 streams; `--profile` forced). Decoders: FAAD2 (`$FAAD -q`), in-tree `$FAAD3 --strict -q`, `ffmpeg -v error -xerror`, `$FDKDEC in out.wav` (plain: this fdkdec has no working `--strict`, as in STNS_VALIDATION).
**80 / 80 decodes: exit 0 and empty stderr.**

EIGHT_SHORT ICS carrying TNS (decision dump; v1 = `FAAC_STNS=1`, v2 = `+FAAC_STNS_ATK=1`; same CBR streams config; ICS counts are identical for v1 and v2 because block switching is untouched). "filt/ICS" = filtered windows per TNS-carrying ICS; v2 is 1.00 by construction.

| clip / config | short ICS | TNS % v1 | filt/ICS v1 | TNS % v2 |
|---|---|---|---|---|
| 13 fatboy lc 32 / 64 / 128 | 1328 / 2008 / 2008 | 79.4 / 73.8 / 74.3 | 3.70 / 2.84 / 2.81 | 62.6 / 50.0 / 52.7 |
| 13 fatboy he-v1 32 / 48 | 1344 / 1344 | 69.5 / 71.4 | 4.20 / 3.96 | 52.4 / 52.5 |
| 01 castanets lc 32 / 64 / 128 | 200 / 246 / 246 | 51.0 / 35.8 / 49.6 | 1.27 / 1.07 / 1.25 | 27.5 / 15.4 / 24.8 |
| 01 castanets he-v1 32 / 48 | 198 / 198 | 38.4 / 30.3 | 1.20 / 1.08 | 23.7 / 17.7 |
| bas lc 32 / 64 / 128 | 36 / 140 / 140 | 63.9 / 57.9 / 58.6 | 1.70 / 1.64 / 1.59 | 16.7 / 19.3 / 19.3 |
| bas he-v1 32 / 48 | 458 / 458 | 50.7 / 48.5 | 2.15 / 2.04 | 17.5 / 17.5 |
| 24 Greensleeves lc 32 / 64 / 128 | 300 / 388 / 388 | 64.7 / 61.9 / 60.3 | 2.82 / 2.16 / 2.14 | 31.7 / 32.7 / 31.7 |
| 24 Greensleeves he-v1 32 / 48 | 402 / 402 | 68.2 / 68.7 | 3.43 / 3.45 | 34.3 / 34.3 |

Reading: v2 roughly halves the share of short ICS with TNS on bas/Greensleeves (the clips that lost under v1) and cuts filtered windows per ICS to exactly 1; on fatboy 74 % -> 50-53 % of ICS but filtered windows drop about 3x (2.8-4.2 -> 1).

## d. Instruction count
callgrind, first 10 s of 13 fatboy (48 kHz stereo), LC 64 kbps ABR `--rate keep`, release frontend builds:

| build / env | Ir | vs master |
|---|---|---|
| master | 130,160,189 | |
| v2 binary, env unset | 130,430,036 | +0.21 % (layout; output identical) |
| v1 set (`FAAC_STNS=1`) | 163,845,528 | +25.9 % |
| v2 set (`+FAAC_STNS_ATK=1`) | 139,285,507 | +7.0 % |

(95 % of ICS in this excerpt are short: worst case. Master Ir differs slightly from STNS_VALIDATION's 129.9 M because this is a fresh 10 s cut and a new build.)

## Footprint
`size -A` libfaac.so.3.0.0, gcc, `-Db_lto=true` release, clean trees:

| section | master | v2 patch | delta |
|---|---|---|---|
| .text | 71616 | 75104 | +3488 |
| .rodata | 10360 | 10456 | +96 |
| .data | 1184 | 1184 | 0 |
| total | 83160 | 86744 | +3584 (+4.3 %) |

(v1 was +1664; the rest of the growth is the energy pass and the extra branch in the short loop, likely inlined/unrolled.)
