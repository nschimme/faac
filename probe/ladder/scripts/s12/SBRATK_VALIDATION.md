# S12 Stage 3 arm B: attack-aligned SBR VARFIX border, scratch patch validation

Patch: `sbr_attack_scratch.patch` (against origin/master 3a656f7; worktree `/home/user/work/sbratk`, build `build`, release, `-Dencoder-ps=true`).
Gate: `FAAC_SBR_ATK=1`, read once in `SbrInit` into `SBRInfo.attackBorder`. Master reference binary for all comparisons: `faac-master/build-ps` (same 3a656f7, PS on).

## What the patch does and the grid facts it relies on
- `SbrAnalyze` already tracks the per-slot energy maximum per channel; the patch keeps the slot index (`atkSlot`) of the channel that sets `frameStrength`. The threshold test is untouched, so the same frames are VARFIX.
- Slot units: analysis slots are 64 samples (`numSamples/64` = 32 per 2048-sample frame); the bitstream's SBR time slots are 128 samples (`SBR_NUM_TIME_SLOTS` = 16 per frame, `env_border` = `t * num_slots / 16` = 2 analysis slots per time slot). Attack time slot = `atkSlot * 16 / num_slots`, border = that rounded down to even (rel border steps are 2 slots).
- `SBR_ANALYSIS_DELAY`: analysis slot s reads input from `64*s - 512`, i.e. the window is already the delayed one in which `env_border` bins the energies, so the delay needs no further correction: the envelope starting at analysis slot `2*border` is the one that holds `atkSlot`. This was checked on the decoded side with a synthetic burst signal (white-noise bursts at 300 samples on a -70 dBFS floor, 60 bursts, HE-v1 32k CBR, in-tree decdump): in the decoded-time convention of `decisions/sbr_grid_he.md` (position = (T + lag - 256 - 2048k)/128) the burst lands 0.3 to 2.3 slots after the chosen border and never before it, for every burst the arm moved.
- Decoder semantics (in-tree `libfaad/sbr.c` `sbr_read_grid`, case VARFIX): `t_E[0] = bs_var_bord_0`, `t_E[i+1] = t_E[i] + 2*bs_rel_bord + 2`, `t_E[L_E] = numTimeSlots`; pointer width `ceil_log2(L_E)` = 2 bits at L_E=2; pointer 0 gives `l_A = -1` and noise border `t_Q[1] = t_E[1]`, so the noise-floor split follows the envelope border with no change to the pointer. The encoder writer (`put_grid`) writes exactly these fields, and the E records of the decode confirm the written border is the decoded one (`E` t_E = 0,B,16).
- Deviation from the brief: leading border stays 0 (borders 2, 4, 6, 8 only). `sbr_hf_generate` starts HF generation at `t_E[0]`; a VARFIX frame with `bs_var_bord_0 > 0` leaves those slots without HF unless the previous frame's trailing border extended past 16 (it is always 16 here). So encodable attack range is border 2..8; attacks in time slots 0-1 and 8-15 keep master's mid border (8 or no change). Time slots 10-15 could only be reached by L>0 or more envelopes, both out of scope.
- Envelope binning and norm: `env_border`/`envSampled` read `sa->tEnv`, so the new border flows through unchanged (also the HE-v2 path; PS parameter binning uses its own 4 phases and is unaffected). Noise floor: one constant level per noise envelope, borders decoder-derived (above).
- Overhead: `put_grid` bits unchanged (all fields fixed width); the border value alone differs.

## a. Unset = master byte-exact: 261/261
Method: `ffmpeg -v error -i X -map 0:a -c copy -f md5 -` of the master (`faac-master/build-ps`) vs patched binary with `FAAC_SBR_ATK` unset, plus equal exit status. Inputs: 10 s excerpts of `01 castanets`, `07 applaud`, `09 Linchpin` at 48000 / 44100 / 32000 Hz (ffmpeg `-ar`), stereo and mono (18 inputs). Configs per input: LC ABR 96, CBR 96, VBR q100; HE-v1 ABR and CBR at 24/32/48/64 and VBR q20/q60; HE-v2 CBR 24/32/48 (stereo only). Result **261/261 identical**, 0 mismatches.

## b. Strict decode with FAAC_SBR_ATK=1: 24/24
HE-v1 CBR 24/32/40/48, full clips `01 castanets`, `07 applaud`, `13 fatboy_30sec`, `09 Linchpin`, `05 Enola`, `03 eig`. Decoders: `$FAAD` (`-q`), in-tree `$FAAD3 --strict -q`, `ffmpeg -v error -xerror`, `$FDKDEC`. Criterion exit 0 and empty stderr.
- Arm stream differs from master's in 24/24 cases (arm active).
- Strict clean on all four decoders: **24/24**.
- Decoded PCM byte length per decoder equal to master's: **24/24** (all four decoders).

## c. Decision dump (in-tree libfaad with `libfaad_decdump_sbrgrid.patch`, `/home/user/work/faad-dump/build-rel`)
Same six clips, HE-v1 CBR, 48 kHz output. (24 and 32 kbps give identical grids; 40 and 48 identical.) VARFIX frame/channel counts and inner border t_E[1] (all VARFIX frames have 2 envelopes), 4840 SBR frame-channels per rung over 6 clips:

| rung | arm | VARFIX | border 2 | 4 | 6 | 8 |
|---|---|---|---|---|---|---|
| 24/32 | master | 1954 | 0 | 0 | 0 | 1954 |
| 24/32 | arm | 1954 | 240 (12.3 %) | 222 (11.4 %) | 208 (10.6 %) | 1284 (65.7 %) |
| 40/48 | master | 326 | 0 | 0 | 0 | 326 |
| 40/48 | arm | 326 | 16 (4.9 %) | 46 (14.1 %) | 22 (6.7 %) | 242 (74.2 %) |

34 % (32k) / 26 % (40-48k) of VARFIX frames move; the rest keep 8 (attack at time slot >= 8 or < 2).

Onsets (merged scored onsets of `dec_streams/ref_onsets.json`, frame k = floor((onset + lag - 256)/2048), pos in slots, interior border within +-2 slots of any onset channel; lag by xcorr per stream). n = onsets with a grid; castanets has only 14 scored onsets and `07 applaud` has none in the scored set.

| rung | clip | master near2 | arm near2 | VARFIX-frame onsets master | VARFIX-frame onsets arm |
|---|---|---|---|---|---|
| 32 | castanets | 4/14 | 5/14 | 4/13 | 5/13 |
| 32 | fatboy | 53/312 | 51/312 | 53/236 | 51/236 |
| 40/48 | castanets | 5/14 | 6/14 | 3/12 | 4/12 |
| 40/48 | fatboy | 221/312 | 223/312 | 4/22 | 6/22 |
| 32 | all 6 clips | 74/392 | 59/392 | 74/305 | 59/305 |
| 40/48 | all 6 clips | 256/392 | 246/392 | 20/71 | 10/71 |

Signed offset (onset position - inner border, slots) in VARFIX frames at 32k, all clips: master is flat (peak 3: 31); arm peaks at +2 (43) and +3 (51) of 305, i.e. the arm border sits 2-3 slots before the reference-convention onset position, while the synthetic bursts sit 0.3-2.3 after it. The near2 share does not improve (castanets +1 onset, fatboy +-2, `03 eig` 16/65 -> 2/65 at 32k all frames). Reading: on real clips the energy-argmax slot is a mean 1-2 slots earlier than the detector's refined onset, plus the floor-to-even rounding, so the +-2 test misses; see open issues.

## d. Cost
- callgrind total Ir, `Linchpin` 10 s, HE-v1 32k CBR (non-LTO release): master 152,541,438; patched unset 152,593,924 (+0.034 %); patched `FAAC_SBR_ATK=1` 152,592,695. `SbrAnalyze` 29,049,778 master vs 29,098,950 unset.
- Footprint, LTO release (`-Db_lto=true`), `libfaac.so` `size -A` (bytes): .text 96032 -> 96208 (+176), .rodata 11484 -> 11468 (-16), .data 1184 -> 1184; sum 108700 -> 108860 (+160).

## Open issues
- Only 26-34 % of VARFIX frames change; attacks in the second half of the frame cannot be reached with L = 0 and two envelopes.
- No improvement of the +-2-slot onset share on the six clips; offset analysis above suggests the border lands early on real material. A +1 time-slot offset (border = even(floor((atkSlot+1)/2))) is the obvious next variant if the NPER arm shows no gain; not implemented here.
- Pointer left at 0 (`l_A = -1`): the decoder keeps its smoothing across the attack envelope; pointer = 2 (transient envelope = envelope 2) would disable it and is untested.
