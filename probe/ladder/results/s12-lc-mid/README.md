# Stage S12: LC 44.1 and 48 kHz against Apple, mid and high bitrates (2026-10-18)

Goal: beat Apple AAC 27.0.1 at 44.1 and 48 kHz stereo LC, 96–320 kbps, judged by BD-rate on the benchmark's
`--gate` clips with the 49-clip corpus as the regression guard. Base: `nschimme/faac` master `a71e7ea`
(contains #52, #56, #57, #58). Result: [PR #108](https://github.com/nschimme/faac/pull/108), merged upstream as #60
(`cadcad7`), which closes about half of the gate gap. Everything below is local (zimtohrli through the benchmark's `score_clip.py`), not CI.
Scripts are in `scripts/`, raw rows and logs in `data/`, probe knobs in `probe-knobs.patch` (never merge).

## 1. The pipeline reproduces the CI table

The CI tables in the task use `--gate`, which scores four clips: sandman, velvet, 21-classic and fms
(`config.py` `_MUSIC_GATE`). Restricting the local 49-clip scores to those four clips matches the CI table to
within 0.002 MOS on every rung (for example 48k/128: local 4.873 vs 4.833, CI 4.873 vs 4.833; 44.1k/128: 4.877 vs
4.844, CI 4.878 vs 4.844). The earlier build label in the CI table (e2130410) and the rerun (c47dea09) are both
after #56 and #57; they differ only by #58 and test commits, so the LC rows are identical by construction.

## 2. The gap is the gate, not the corpus

Master against Apple on all 49 clips (Apple minus FAAC mean MOS; positive means Apple leads):

| Rung | gap | Apple wins / FAAC wins | Apple bytes vs FAAC |
|---|---|---|---|
| 44.1k 128 | -0.005 | 22 / 27 | +1.7% |
| 44.1k 160 | -0.003 | 24 / 25 | +1.4% |
| 44.1k 192 | +0.002 | 34 / 15 | +1.3% |
| 44.1k 256 | -0.001 | 17 / 32 | +1.4% |
| 48k 96 | -0.006 | 26 / 23 | +2.1% |
| 48k 128 | -0.004 | 25 / 24 | +2.1% |
| 48k 160 | +0.001 | 26 / 23 | +1.5% |
| 48k 192 | +0.006 | 41 / 8 | +1.4% |
| 48k 256 | +0.001 | 28 / 21 | +2.7% |
| 48k 320 | +0.002 | 33 / 16 | +3.9% |

The 1st-percentile gap in the CI table (Apple +0.07 at 128k) is also a four-clip effect. BD-rate against Apple
(positive means FAAC needs more bits): gate +5.41% (48k) and +6.03% (44.1k); all 49 clips -1.38% and -1.78%.

Velvet carries most of the raw gate gap and it is bytes: Apple spends 378 bytes per frame on velvet at 128k against
FAAC's 344 (+10%), while FAAC's ABR spends almost exactly the nominal amount on every clip (about 162 kB for the
10 s clips at 128k). FAAC's own slope on velvet is 0.64 MOS per doubling, so the bytes-adjusted gap at 128k is
-0.004. The bit-neutral gaps are fms (+0.046 at 128k) and 21-classic (+0.020).

## 3. What Apple does differently (FAAD dumps, 49 clips, 128k, mean over clips)

| | FAAC | Apple |
|---|---|---|
| short-block share of ICS | 17.2% | 9.5% |
| long-block bandwidth (max_sfb) | 18.75 kHz | 20.25 kHz |
| 12–24 kHz bands PNS / zero | 82% / 3% | 0% / 51% |
| 12–24 kHz nonzero lines per long ICS | 13.7 | 44.6 |
| 2–6 kHz nonzero lines per long ICS | 101.8 | 85.3 |
| M/S share of coded bands, 6–12 kHz | 90.9% | 77.1% |
| mean scalefactor 0–2 kHz | 151.5 | 154.8 |

The pattern repeats at 160k and 192k. These differences are descriptive: the null results below show the first
two do not translate into BD-rate by themselves.

## 4. Null results (gate BD-rate, 48k / 44.1k; master +5.41% / +6.03%)

- Block switching (`FAAC_BS_*`, 13 variants: level ratio 3/4/6, drop ratio 20/50, smoothing 0.15/0.5, context span
  combinations): best 48k +4.85% (smoothing 0.5), best 44.1k +5.87% (span 2/1). Smoothing 0.15 is worse (+6.7%/+7.1%).
  Nothing separates from noise.
- Bandwidth (`-c`): 17 kHz +8.5%/+10.2%, 20 kHz +5.6%/+7.1%, 20.5 kHz +5.8%/+7.4%, 22 kHz +6.9%/+7.4%. The default
  cutoff is near the optimum.
- Rate-control account (`FAAC_RC_*`): depth 8/16/32 frames is identical to 4 (the bound never binds); settling time
  16 and 8 frames are worse (+5.7%/+6.5% and +6.5%/+7.3%).
- PNS and the scalefactor search were not rerun (closed earlier).

## 5. Masking parameters: what worked

`FAAC_MM` exposes the #56 masking model. One at a time, on the gate (avg of the two rates, master +5.72%):
peak floor 0.010 (+4.78%), loudness exponent 0.30 (+4.82%), average weight 0.10 (+5.18%), treble tilt -1.5 dB above
5 kHz (+5.19%), slope multiplier 2.0 (+5.22%), rich slope 4 (+5.29%) all helped at both rates; the opposite
direction of each hurt (peak floor 0.002 +7.47%, exponent 0.40 +7.41%). Combined (gate, 48k / 44.1k):

| Combination | 48k | 44.1k |
|---|---|---|
| pf 0.01, le 0.30 | +4.71% | +4.80% |
| + aw 0.1, ss 2.0, tilt -1.5 dB >5 kHz, rs 4 ("full") | +2.47% | +2.51% |
| pf 0.01, le 0.30, ss 2.5 ("min") | +2.82% | +3.25% |
| pf 0.01, le 0.30, ss 3.0 (shipped) | +2.51% | +3.01% |
| pf 0.02 or le 0.25 | worse than pf 0.01, le 0.30 | |

The optimum in `pf` and `le` is sharp; `ss` is flat from 3.0 to 3.5. Shipped in PR #108: `pf` 0.010 (moved into the
profile so the default profile is untouched), `le` 0.30, `ss` 3.0.

Guard and scope (the real patched binary): 

| | master | PR #108 |
|---|---|---|
| gate 48k / 44.1k | +5.41% / +6.03% | +2.51% / +3.01% |
| 49 clips 48k / 44.1k | -1.38% / -1.78% | -2.42% / -2.58% |
| 64k LC mean MOS, 48k / 44.1k | 4.468 / 4.483 | 4.495 / 4.506 |
| 80k LC mean MOS, 48k / 44.1k | 4.613 / 4.625 | 4.627 / 4.635 |

Encoded files are byte-identical to master for HE 32/48k, mono LC 64/128k, stereo LC 56k and VBR (it is not
the tuned profile). 43 of 49 clips improve at 48k and 38 of 49 at 44.1k; the largest single-clip regression is
+8.4% (48k) and +5.6% (44.1k).

Per clip after the PR (BD-rate vs Apple, master to PR): velvet 48k +1.98% to -4.40%, 44.1k +10.42% to +1.60%;
sandman 48k +0.16% to -0.72%; fms 48k +12.94% to +9.17%, 44.1k +3.71% to +1.09%; 21-classic 48k +6.58% to
+6.00%, 44.1k +8.86% to +8.69%.

## 6. Where the loss is now, and suggested next steps

Worst clips against Apple after the PR (BD-rate; none except fms and 21-classic are in the gate):

| Clip | 48k | 44.1k |
|---|---|---|
| SinceAlways | +19.8% | +18.9% |
| take_your_finger_frin_my_head | +14.0% | +17.5% |
| 35_SQAM_glockenspiel_cut | +14.4% | +10.3% |
| fms | +9.2% | +1.1% |
| Girl_In_The_Fire | +9.1% | |
| TrosYGareg | +8.2% | |
| 21-classic | +6.0% | +8.7% |

These clips set the 1st percentile and are the same on both sample rates, so the cause is the clip, not the
rate. SinceAlways, take_your_finger and the glockenspiel are the gap; the gate no longer sees them.

Suggested order:

1. Read CI for #108 first (ABR amd64, CBR arm64, VBR arm64). If it holds, the profile is the new baseline for
   everything below. Re-measure the "full" extras (`aw` 0.1, tilt -1.5 dB above 5 kHz, `rs` 4: gate 48k +2.47%,
   44.1k +2.51%, about 0.5% better at 44.1k) as a second small PR only if CI shows headroom.
2. Fit the masking model against all 49 clips with a held-out split (even clips fit, odd clips test, and the
   reverse) instead of the four gate clips. The sweep above was one-at-a-time and the gains added almost
   linearly, so a coordinate-descent over `pw, af, qb, sm, tilt knots 0-7, aw, le, ss, rs, pf` on mean BD-rate
   at both rates is cheap (about 6 minutes per 49-clip evaluation) and exposes interactions. Treat the per-clip
   worst-case (the table above) as a second objective so the mean cannot hide the tail.
3. Analyse the three worst clips directly (SinceAlways, take_your_finger, glockenspiel) with `analyze.py` against
   the Apple dumps, as was done for the gate. None of them has been looked at yet.
4. VBR LC: `bitrate_per_ch` is 0 for VBR, so VBR never selects the tuned profile and #108 does not touch it. Check
   whether the tuned profile helps the VBR ladder (needs an Apple comparison at matched bytes; BD-rate handles that).
5. Time-domain allocation that is complexity-aware: Apple varies bytes per clip (velvet +10%) while FAAC's ABR
   is flat, and FAAC's slope on velvet is steep. The account-depth sweep above was null, but it only loosened the
   same flat target. A per-frame target that follows a perceptual-entropy proxy is a different experiment.
6. TNS: Apple spends about twice FAAC's TNS side bits on fms and velvet; the gating thresholds were not swept.
7. Held-out validation: the Apple references only cover these 49 clips. A claim of beating Apple outside them
   needs new clips plus matching Apple encodes on macOS.

## Reproduction

`run.sh` / `run2.sh`: encode with master FAAC and dump FAAC and Apple streams through the `add-faad-decoder`
FAAD3 build (`-Dstats=true`, `FAAD_DUMP`). `analyze.py`, `gap.py`: structure and per-clip gaps. `mkstore.py`
builds `data/store.json` (Apple and master curves); `bd.py` computes the benchmark's PCHIP BD-rate; `var.py`
encodes and scores a variant over the ladder and prints BD-rate; the `sweep_*.sh`, `guard.sh`, `low.sh` and
`final.sh` scripts are the exact runs behind the logs in `data/`. Paths are hard-coded to `/home/user/work/an`,
`/opt/faac-benchmark` and `/opt/venv312`. Apple references are `ref/apple-s11-367cf8dc` (48k 96–192k) and
`ref/apple-s11-extra` (44.1k 128–256k, 48k 256/320k).

## 7. Follow-up (same day): held-out fit and the worst clips

**Coordinate descent on the whole masking model** (`scripts/cd.py`, `data/cd_*.log`, `data/cd_cache.json`). Start at the
shipped values, objective = mean BD-rate against Apple (average of the 48k and 44.1k family means; 48k rungs
96-256k, 44.1k rungs 128-256k), step each of 12 parameters up and down (`le`, `pf`, `ss`, `aw`, `pw`, `qb`, `af`, `sm`,
three tilt bands, `rs`), accept a move only if it gains more than 0.15 points. Fold A fits the 25 even-indexed clips and
tests on the 24 odd ones. Result: no move was accepted. Fit BD stays -3.725% and held-out BD stays -1.141% (the
two numbers differ because the halves contain different clips). The largest single-step gains were `pw` 0.45 to 0.346
(-0.12), `le` 0.30 to 0.28 (-0.10), `pf` 0.010 to 0.014 (-0.085), all below the threshold, and
`sm`, `qb`, the three tilt bands and `rs` were each worse or flat in both directions. The shipped point is a local
optimum of this model on this half of the corpus. Fold B (fit odd, test even, `data/cd_B.log`) accepted one move in pass 1,
`pw` (peak_weight) 0.45 to 0.346: fit -1.141% to -1.323%, and the held-out even half -3.725% to -3.846%, so both halves
agree on the direction. Its half-step second pass (`le` +-0.01, `pf` +-17%) rejected every move so far.

**`pw` 0.346 on the full ladder is not worth shipping** (`scripts/pw.sh`, `data/pw.log`, BD-rate vs Apple): gate 48k
+2.51% to +2.64% and 44.1k +3.01% to +3.30% (worse), all 49 clips 48k -2.42% to -2.62% and 44.1k -2.58% to -2.72%
(better), 64k/80k LC mean MOS within -0.0009 to +0.0019 of the shipped values. The gain is on the full corpus and the loss
on the gate, both under 0.3 points, and `peak_weight` is global (it would also change HE and mono unless moved into the
profile). Treat it as a null result.

**Worst clips against Apple after #60** (128k dumps, BD-rate; none except fms and 21-classic are in the gate):

| Clip | Signature at 128k | Reading |
|---|---|---|
| SinceAlways | FAAC 129 nonzero lines at 2-6 kHz against Apple's 80; 12+ kHz: FAAC 7 lines and 92% PNS, Apple 57 lines and 0% PNS | MOS gap +0.057 at 128k, +0.026 at 192k, +0.003 at 320k |
| take_your_finger | FAAC 107 vs 76 lines at 2-6 kHz; 12+ kHz: 1 line and 98% PNS vs 29 lines | MOS gap +0.082 at 128k, +0.019 at 192k |
| 35_SQAM_glockenspiel | FAAC undershoots the nominal rate (about -13% at 320k, -7% at 192k) | MOS gap only 0.007 to 0.017 but MOS is 4.98-4.99, where BD-rate turns a tiny MOS difference into a large percentage; low priority |

**Fixed treble targets do not fix them** (`scripts/worst.sh`, `data/w_*`; BD-rate vs Apple, mean of 6 clips: shipped 9.84%
(48k) and 9.68% (44.1k)). Raising the treble target by +3 dB above 11 kHz: 10.67% / 10.47%; +5 dB: 13.41% / 12.31%;
`rs` 2: 10.54% / 9.98%. take_your_finger improves (48k +14.0% to +8.1% at +5 dB, 44.1k +17.5% to +12.2%), while velvet
(48k -4.40% to +6.38%), fms and 21-classic get worse and SinceAlways does not respond (+19.8% to +23.6%). A
fixed tilt trades clips against each other; only a rule that follows the clip could help take_your_finger, and
SinceAlways has a different cause (the 2-6 kHz share).

Revised next steps, in order:

1. Read CI for #60 on master (CBR and VBR ladders, per-clip gate).
2. SinceAlways: test lower 2-6 kHz targets on that clip alone (the `tlo`/`tmid` bands were only moved by 1 dB on the mean,
   which cannot show a clip-specific effect), and look for what separates it from the clips that gain from the retune.
3. A content-adaptive treble target for take_your_finger-like material (high-frequency energy relative to the frame),
   gated so velvet, fms and 21-classic keep the shipped behaviour.
4. VBR LC (never uses the tuned profile), TNS gating, complexity-aware per-frame allocation, held-out clips: as in section 6.
