# Item 1 CBR v2: one-sided reservoir spend, scoped to stereo >= 64 kbps/ch (fix for PR #208's mos gate)

**Five-line result**
1. Unscoped v2 (spend only above half full, every CBR stream) cut PR #208's 35 failing cells to 18, all in mono speech
   16k 20/24/VoIP 24 (16), 24k mono 32 (1) and HE 48k stereo 24 (1); means flat (CI spans 0), swings symmetric (e.g. 16k
   mono 20k: 4 clips > +0.10, 7 < -0.10), i.e. low-rate rate-control chaos, not a mean loss -- still fails the gate.
2. Scoped per the prereg's fallback: the rule runs only for CBR stereo at >= 64 kbps/ch (`resSpend`). Every
   scenario below that (all mono, all stereo <= 96k, HE-v2 at any rate) is PCM-identical to master: 0 gate cells by
   construction (85/85 encodes checked, FAAD2 decode cmp).
3. Where it runs (48k stereo 128/192, LC and HE-v1), FAAD3 runner vs master: LC +0.0030 [+0.0023,+0.0037] at 128k,
   +0.0010 [+0.0008,+0.0011] at 192k, HE-v1 +0.0008 / +0.0001; worst clip -0.011; no cell below -0.05.
4. ABR/VBR PCM-identical to master (16/16); realized rate +0.07..+0.15 kbps; callgrind 10 s CBR velvet 192k +4.4 %,
   64k +1.1 %, Bachpsichord +0.3 / +0.1 % (v1's -50 % is gone: below half the aim is master's again).
5. LC CBR 128-320, 77 clips, FAAD2/FFmpeg/fdkdec: BD-rate vs master -0.73/-0.75/-0.76 % (PCHIP; linear -0.75..-0.78),
   CIs [-0.84,-0.64]-ish, 72-74/77 clips better, no cell below -0.02 (worst 10xh_ 128k -0.018); vs Apple -1.6..-1.8 %
   (CI excludes 0). Same gain as v1 with tighter CIs. Footprint (release, encoder-ps, non-LTO `size`): .text +256 B
   (faac_encoder_open +208 from layout, faacEncEncode +38) -- flagged as growth for the owner.

Patch: `pr/cbr_reservoir_midpoint_v2.patch` (replaces v1). Prereg: `PREREG_v2.md` addendum 2 (written before scoring).
Runner rows (benchmark `compare_codecs`, FAAC only, FAAD3, all three profiles, master 3a656f77 vs `-dirty` =
unscoped v2): `v2_runner/rows_runner_faad3.jsonl`; per scenario x profile table `v2_runner/unscoped_v2_vs_master.md`.

## LC CBR 128-320, three decoders (scoped v2 vs master)
Rows `rows_item1_cbr_v2.jsonl` (arm `cbrv2`, MOS only); per rung table `armcmp_v2.md`; BD-rate `bd/cbrv2_vs_*`.
Mean dMOS per rung (FAAD2): +0.0029 / +0.0016 / +0.0009 / +0.0005 / +0.0002 at 128/160/192/256/320; realized rate
+0.12 / +0.06 / +0.04 / +0.01 / +0.00 %. Note: a first BD pass omitted `--mode cbr` and mixed master ABR rows into
the reference (+0.9 %, wrong); the numbers above restrict both sides to CBR.

**Verdict: PR-ready** (replace PR #208's diff with `pr/cbr_reservoir_midpoint_v2.patch`), subject to the owner's call
on the +256 B footprint.
