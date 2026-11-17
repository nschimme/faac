# S12 item 1 prereg: CBR rate-control aim (written before scoring)

## Evidence (no MOS read for this arm)
- Leaderboard mode is CBR (`compare_codecs.py` 759-765). Against the same Apple streams FAAC ABR is ~1.5 % more
  efficient than FAAC CBR (stage 1: ABR 128-320 -2.5..-2.7 %, CBR about -1 %, unresolved), and in CBR FAAC trails
  Apple at equal realized rate at 192k (-0.0027) and 320k (-0.0012).
- `ratecontrol.c` aims every frame at `RC_BALANCE_AIM` = 0.99 of the budget (chosen for ABR so the average lands just
  under nominal). Under CBR the reservoir floor forces the nominal rate anyway, so the 1 % under-aim reaches the
  stream as fill: a stats build (`-Dstats=true`) reports stuffing of 0.90-1.34 % of the stream at 192k and 320k CBR on
  velvet, fatboy, 11ff_ and Bachpsichord, i.e. about the under-aim. Velvet additionally runs the reservoir empty (bound
  retries 54-70 % of frames).
- Not a protected knob (loudness exponent, PEAKW, scalefactor +-1 search are untouched).

## Arm
Scratch patch `scripts/s12/cbr_aim_scratch.patch`: `FAAC_CBR_AIM` replaces the aim only when the CBR reservoir is
active; unset = master (audio payload md5 identical on 30/30 LC ABR/CBR/VBR, HE-v1, HE-v2 encodes). Arm `cbr100`:
`FAAC_CBR_AIM=1.0`. One value, fixed now; any tuning happens on the full dataset.

## Measurement
- FAAC CBR vs FAAC CBR (control = stage 1 `rows_lc_cbr.jsonl`, same binary output when unset), LC 48 kHz stereo
  128/160/192/256/320, 77 clips, FAAD2/FFmpeg/fdkdec, realized kbps; stuffing share on 10 clips from a stats build.
- Reading order: worst clip x rung vs control (gate -0.10); realized kbps per rung (must stay within the benchmark's
  bitrate-error band of the nominal rate); BD-rate vs control and vs Apple, PCHIP and linear with sign counts; per-rung
  dMOS vs Apple at equal realized rate.
- Advance to HE CBR, 44.1/32 kHz CBR and the full-dataset pass only if: no cell below -0.10, both BD methods vs control
  have CIs wholly below 0 under all three decoders (or, if realized rate rises, dMOS at equal realized rate is positive
  with CI above 0), and mean realized rate stays within 0.5 % of nominal. Otherwise close as negative.

## Addendum 2026-11-17 (mechanism data, before any MOS row was read)
Stats build of the scratch patch, 10 clips (`reservoir_stats.tsv`), CBR, mean over clips:

| rate | aim | stuffing % of stream | bound retries % of frames | min reservoir fill % | underflows |
|---|---|---|---|---|---|
| 192 | 0.99 | 0.97 | 9.5 | 11.6 | 0 |
| 192 | 1.0 | 0.16 | 38.0 | 5.2 | 0 |
| 320 | 0.99 | 1.41 | 22.5 | 0.1 | 0 |
| 320 | 1.0 | 0.80 | 71.5 | 0.0 | 0 |

Aim 1.0 recovers most of the fill at 192k but makes 4x as many frames bust the reservoir cap and re-quantize.
callgrind (10 s, 192k CBR): velvet 504.2M -> 873.8M Ir (+73 %); Bachpsichord 171.3M -> 171.6M (+0.2 %). No frame
exceeded the legal buffer (0 underflows). The retry cost alone fails the throughput gate on reservoir-starved clips,
so even a MOS pass would need a cheaper form (e.g. aim at the reservoir's fill target instead of a fixed ratio, or a
first-pass quality correction from the cap) before any PR; that would be a new arm with its own prereg.
