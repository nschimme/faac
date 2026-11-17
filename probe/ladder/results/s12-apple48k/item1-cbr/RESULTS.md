# Item 1 CBR aim: result of arm `cbr100` (FAAC_CBR_AIM=1.0), LC 48 kHz stereo CBR 128-320

Prereg `PREREG.md` (+ mechanism addendum). Rows `rows_item1_cbr.jsonl` (1155 rows, 0 errors); control = stage 1
`rows_lc_cbr.jsonl`. Tables: `armcmp.md`, `bd/*.md|json`.

## Worst cells (vs control, per decoder)
No cell below -0.10 at any rung under FAAD2, FFmpeg or fdkdec. Worst: 08 velvet -0.070 (128k), -0.047 (160k), -0.022
(192k), -0.010 (256k), -0.003 (320k), all three decoders alike: the reservoir-starved clip gives back quality as the
reservoir runs empty more often.

## Rate, MOS, BD-rate
| rung | dMOS vs control (FAAD2) | realized kbps change |
|---|---|---|
| 128 | +0.0032 | +0.17 % |
| 160 | +0.0013 | +0.11 % |
| 192 | +0.0008 | +0.07 % |
| 256 | +0.0005 | +0.03 % |
| 320 | +0.0002 | +0.01 % |

BD-rate vs control 128-320: PCHIP -0.80 [-1.12, -0.42] (5 worse / 72 better), linear -0.81 [-1.13, -0.43] (FAAD2);
FFmpeg -0.81 / -0.83, fdkdec -0.81 / -0.81, all CIs below 0. Pooled NPER unchanged (within 0.02 dB).
vs Apple 128-320: master CBR PCHIP -1.17 [-2.80, +0.40] / linear -0.99 [-2.61, +0.59] (unresolved) -> arm PCHIP
-1.89 [-3.56, -0.24] / linear -1.70 [-3.36, -0.06] (resolved, all three decoders). dMOS vs Apple at nominal rung
(FAAD2): +0.023 / +0.008 / -0.0022 / +0.0024 / -0.0010 at 128 / 160 / 192 / 256 / 320 (192k still 20 better / 57 worse).

## Verdict
Passes the LC CBR prereg (no cell < -0.10, both BD methods' CIs below 0 under all three decoders, rate within 0.5 %).
**Not PR-ready**: the mechanism addendum measured +73 % instructions on velvet (bound retries 9.5 % -> 38 % at 192k,
22.5 % -> 71.5 % at 320k), which fails the throughput gate, and velvet itself loses 0.07 at 128k. Next: a CBR aim
that steers the reservoir to its middle instead of a fixed 1.0 (`PREREG_v2.md`), meant to keep the fill recovery
without the cap busts. HE CBR, 44.1/32 kHz and the full-dataset pass wait for the cheaper form.

# Arm `cbrmid` (v2, `PREREG_v2.md`): reservoir-centred CBR aim, LC CBR 128-320

Rows `rows_item1_cbr.jsonl` (arm cbrmid), control `stage1-rebaseline/rows_lc_cbr.jsonl`; `armcmp_mid.md`, `bd/cbrmid_*`.

- Worst cells (all decoders): -0.0095 (fms, 160k), -0.0061 (Greensleeves, 160k), -0.0052 (27-last-song, 128k). None
  below -0.10; none below -0.01.
- Velvet (reservoir-starved; -0.070 under cbr100): dMOS vs master -0.002 / +0.010 / +0.023 / +0.004 /
  +0.001 at 128 / 160 / 192 / 256 / 320 (FAAD2).
- dMOS vs master (FAAD2): +0.0035 / +0.0014 / +0.0011 / +0.0006 / +0.0002; realized rate +0.12 / +0.07 / +0.04 / +0.01
  / +0.00 %.
- BD-rate vs master CBR 128-320: FAAD2 PCHIP -0.69 [-0.85, -0.51] (7 worse / 70 better), linear -0.72 [-0.88, -0.54];
  FFmpeg -0.70 / -0.73; fdkdec -0.72 / -0.75; all CIs below 0.
- vs Apple 128-320: PCHIP -1.81 [-3.42, -0.27], linear -1.63 [-3.25, -0.08] (FAAD2; FFmpeg/fdkdec within 0.03), resolved
  (master CBR was not). dMOS vs Apple at nominal: +0.023 / +0.008 / -0.0019 / +0.0024 / -0.0010; 192k 19 better / 58 worse.
- Mechanism (`reservoir_stats_mid.tsv`): stuffing 0.97 -> 0.32 % at 192k; bound retries 9.5 -> 4.9 %; 0 underflows.
  callgrind 10 s 192k CBR: velvet -50 %, Bachpsichord +0.3 %.

Verdict: **passes the LC CBR prereg** (no cell below -0.10, both BD methods below 0 under all three decoders, rate
within 0.5 %, velvet Ir within +5 % of master: -50 %). PR candidate pending the prereg's follow-on passes (HE-v1/HE-v2
CBR, 44.1/32 kHz stereo CBR, 16/24 kHz mono speech CBR, then the benchmark's full-dataset runner), which the parent has
scheduled behind the transient arms. Item 1's 192k/320k cells are not closed by it: the gap to Apple there shrinks from
-0.0027 to -0.0019 (192k) and stays -0.0010 (320k).

## PR form (`pr/cbr_reservoir_midpoint.patch`, 7 lines in `ratecontrol.c`, no getenv, no new field)
Keys off the existing CBR reservoir flag. Audio payload identical to the scratch `cbrmid` build on 21/21 CBR encodes
(LC 64/128/192/320, HE-v1 32/48, HE-v2 24; velvet, 11ff_, fatboy) and to master on 18/18 ABR/VBR encodes.
Footprint (gcc 13 LTO, `size -A` .text+.rodata+.data of libfaac.so): 83160 -> 83368 B (**+208 B**), against the
owner's no-growth rule; an offset is still to be found. PR_BODY.md waits for the follow-on CBR passes and the
full-dataset runner.
