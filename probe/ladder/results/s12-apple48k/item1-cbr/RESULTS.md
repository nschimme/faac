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
