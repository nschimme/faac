# S12: short-window TNS scratch patch (env-gated)

Patch: `stns_scratch.patch` (`git diff origin/master` of worktree /home/user/work/stns, base 3a656f7).
Files: libfaac/{tns.c,tns.h,coder.h,frame.c,channels.c}. Research only; nothing is wired to the public API.

## What it does
- `TnsInit` (runs once per open, from `faacEncApplyConfig`) reads the env and latches the result per channel in `TnsInfo`; nothing is read per frame.
- Knobs: `FAAC_STNS=1` enables; `FAAC_STNS_ORDER` (default 4, clamped 1..7, the 3-bit short order field); `FAAC_STNS_GAIN` (default 1.4, used for both the pre-quantization LPC gain bar and the post-quantization measured-gain bar, as master does for long); `FAAC_STNS_HZ` (start frequency, default 1500); `FAAC_STNS_PROFILE=lc|he|all` (default all; `he` = HE-AAC core, i.e. `IsSBR`).
- Start band: master's long start is a fixed sfb index per rate (`tns_sfb_range[].min`), not a frequency, so there was nothing to reuse. Short start = first short sfb whose lower edge is at or above `FAAC_STNS_HZ` at the core rate (48 kHz, 1500 Hz: sfb 2, line 8). Stop band = the spec's short TNS max-bands table ({9,9,10,14,14,...} by sample-rate index, from memory of the standard, clamped to `max_cbs`). Filter `length` = num_swb_short - start, like long.
- `TnsEncodeShort` is called from frame.c for ONLY_SHORT_WINDOW (non-LFE) when enabled. Layout confirmed: `BlocGroup` only picks group lengths (and zeroes lines above the cutoff); at TNS time `freqBuff` is still 8 contiguous 128-line windows, so window w is `spec + w*128`. Grouping/interleave happens later in quantization. Each window gets an independent filter (syntax is per window).
- The long-path fitter `tns_fit_range` was parametrized (order, two gain bars) and long passes its old constants, so the long path is unchanged.
- `TnsInfo` gains `shortWin[8]` plus the latched settings; `WriteICS` writes the EIGHT_SHORT syntax (n_filt 1 bit; coef_res 1 bit; length 4, order 3; direction, compress; coefs at 4 - compress bits) per window.
- No attack gate for short windows (the long gate is calibrated to long-block sub-block statistics); the SFM/PNS skip and energy checks inside `tns_fit_range` apply as for long. Direction fixed 0 as for long.

## a. Byte-exactness, env unset
Matrix: 2 clips (01 castanets, 05 Enola, first 6 s) x 4 rates (48000, 44100, 32000, 16000) x mono/stereo = 16 inputs; profiles lc, he-v1, he-v2 (he-v2 via build-ps for both master and patched); ABR `-b` 16/32/64/128/256, CBR `-b N --cbr` same rates, VBR `-q` 50/100/200; ADTS output, file md5. Baseline = /home/user/work/faac-master binaries (3a656f7), patched = this worktree's `build`/`build-ps`, `FAAC_STNS` explicitly unset.
- 624 combinations; 422 encode successfully (the rest are rejected by the frontend with rc=1 for invalid rate/profile/channel combos, and rc=1 in both builds).
- **Byte-identical: 422 / 422** successful encodes; 624 / 624 identical including identical exit status.
- Unit/regression suite (`meson test -C build`): 156/156 pass.
- Gating: `FAAC_STNS=1 FAAC_STNS_PROFILE=lc` leaves an he-v1 stream identical to master, `=he` leaves an LC stream identical to master; each differs from master for its own profile.

## b. FAAC_STNS=1 decode cleanliness
Clips 07 applaud, 01 castanets, 13 fatboy_30sec, 08 velvet (48 kHz stereo); configs LC 32/64/128 and HE-v1 32/48 kbps (`--profile` forced; note `--rate auto` makes LC 32 kbps code at 32 kHz). Decoders: FAAD2 (nschimme 287c00c), in-tree faad `--strict`, `ffmpeg -v error -xerror`, fdkdec. Note: this fdkdec has no working `--strict` (it returns 1 on every stream, including a libfdk-encoded one and master streams), so it is run plain and judged on exit code.
- **20 / 20 streams: rc 0 and empty stderr on all four decoders**, and each decoder's output length equals that decoder's output on the master stream of the same clip/config (e.g. 07 lc 64: faad2 1589292 B, faad3 1589328, ffmpeg 1589326, fdk 1584444, identical to master's).
- Extra, all matrix inputs (16 inputs x lc/he-v1/he-v2 x 32/64 kbps, 68 streams) with the aggressive settings `FAAC_STNS_ORDER=7 FAAC_STNS_HZ=0 FAAC_STNS_GAIN=1.1`: 68 / 68 streams differ from master and 0 decoder failures (faad2, faad3 --strict, ffmpeg -xerror, fdkdec), including 16/32/44.1/48 kHz and PS.
- Waveform SNR vs source (aligned, 48 kHz) is within 0.01-0.1 dB of master for 07/08 and lower by 0.2-1.4 dB on 01/13 (long and short TNS shape noise, so waveform SNR is not the right metric; it only shows the filters invert correctly: a wrong direction or inversion would collapse it). Per config: `07 lc32 3.64 -> 3.59, lc64 10.67 -> 10.55, lc128 18.40 -> 18.41; 01 lc64 7.97 -> 7.32; 13 lc64 7.60 -> 6.27, lc128 14.98 -> 13.88; 08 lc64 12.73 -> 12.72`.
- Stream sizes (ADTS bytes, master -> STNS): 07 lc64 68390 -> 68410, 01 lc64 55085 -> 55170, 13 lc32 131242 -> 140352 (ABR landing differs), 08 lc64 98473 -> 98494.

TNS present on EIGHT_SHORT ICS (decoder dump, `decdump.sh` + `decstats.py`, default order 4; long ICS TNS% is master behavior):

| stream | EIGHT_SHORT ICS | TNS % (short) | max order seen |
|---|---|---|---|
| 07 applaud lc 32 / 64 / 128 | 176 / 282 / 282 | 44.3 / 36.9 / 46.8 | 4 |
| 07 applaud he-v1 32 / 48 | 382 / 382 | 25.4 / 20.4 | 4 |
| 01 castanets lc 32 / 64 / 128 | 200 / 246 / 246 | 51.0 / 35.8 / 49.6 | 4 |
| 01 castanets he-v1 32 / 48 | 198 / 198 | 38.4 / 30.3 | 4 |
| 13 fatboy lc 32 / 64 / 128 | 1328 / 2008 / 2008 | 79.4 / 73.8 / 74.3 | 4 |
| 13 fatboy he-v1 32 / 48 | 1344 / 1344 | 69.5 / 71.4 | 4 |
| 08 velvet lc 32 / 64 / 128 | 526 / 482 / 482 | 15.8 / 15.6 / 17.2 | 4 |
| 08 velvet he-v1 32 / 48 | 560 / 560 | 20.2 / 19.8 | 4 |

(Shares are of ICS, not windows: a short ICS counts as TNS if any of its 8 windows has a filter; most carry 1-4 filtered windows.) With `FAAC_STNS_ORDER=7 FAAC_STNS_HZ=0 FAAC_STNS_GAIN=1.1` on castanets lc 64: 89.8 % of short ICS, orders up to 7 (7: 338, 6: 190 filters), decodes clean.

## c. Synthetic pre-echo (direction sanity only)
5 s 48 kHz stereo, 9 white-noise bursts (4 ms decay constant, 30 ms long, peak ~0.6) every 0.5 s on digital silence; LC 32 kbps `--rate keep`; FAAD2 decode, aligned by xcorr (lag 1024). Pre-echo = energy in the 5 ms (240 samples) before each onset, summed over 9 bursts; source value is exactly 0.

| variant | bytes | pre-echo / burst energy | pre-echo vs master |
|---|---|---|---|
| master | 26705 | -20.4 dB | 0.00 dB |
| FAAC_STNS=1 | 26690 | -20.4 dB | -0.01 dB |
| + GAIN=1.1 | 26690 | -20.4 dB | -0.01 dB |
| + ORDER=7 GAIN=1.1 HZ=0 | 26735 | -20.4 dB | -0.01 dB |

No measurable effect. Decoder dump: 428 of 472 ICS are short (the silence/burst frames), only 6 (1.4 %) carry TNS: most short windows are silent (energy gate) or noise-like (SFM gate), and in the burst windows the 128-line frequency resolution leaves nothing to whiten that a 256-sample window does not already confine. Direction is right (no regression, streams decode, identical pre-echo to within 0.01 dB), but this signal shows no benefit; it is not evidence either way for real transients.

## d. Instruction count
callgrind, 10 s of 13 fatboy (48 kHz stereo), LC 64 kbps `--rate keep`, frontend binaries from the release builds:

| build | Ir | vs master |
|---|---|---|
| master | 129,945,799 | |
| patched, env unset | 129,788,133 | -0.12 % (layout noise; outputs byte-identical) |
| patched, FAAC_STNS=1 | 163,155,191 | +25.6 % (this clip is short-heavy; 8 LPC fits per short ICS) |

## e. Footprint
`size -A` on libfaac.so.3.0.0, gcc 13, `-Db_lto=true` release, both built from clean trees (stns-base = origin/master worktree):

| section | master | patched | delta |
|---|---|---|---|
| .text | 71616 | 73184 | +1568 |
| .rodata | 10360 | 10456 | +96 |
| .data (+ .data.rel.ro 96) | 1184 | 1184 | 0 |
| total of the three | 83160 | 84824 | +1664 (+2.0 %) |

Per-channel `TnsInfo` grows by about 230 bytes (8 short windows x one filter).

## Open issues
- Short TNS max-bands table and start-frequency rule are my own choices (documented above); not cross-checked against a decoder that rejects out-of-range bands, but all four decoders accept every stream.
- fdkdec `--strict` unusable here (fails on any stream); plain fdkdec used.
- The synthetic burst test shows no pre-echo gain; real benefit has to be judged on corpus scoring.
- Short-window TNS runs after `BlocGroup`, so grouping decisions see unfiltered spectra; the quantizer/rate loop is unchanged and unaware of the extra side-info bits.
