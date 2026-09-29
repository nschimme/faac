# Stage S4 pre-registered decision rules (written before each run)

## H1 SBR crossover sweep (2026-09-29 ~21:30 UTC, before scoring)
Base: static probe, FAAC_SF_SMOOTH=0.6 FAAC_BS_DROPRATIO=12 (= master + #595 + #599), HE-AAC (auto picks HE at 32k and
48k, 48 kHz stereo), ABR, rate loop on. Knob FAAC_SBR_START (production 15 = kx 31, 11.6 kHz; kx read back from
FAAC_SBR_DUMPTAB). Arms: 8 (kx 16, 6.0 kHz), 11 (kx 20, 7.5 kHz), 13 (kx 24, 9.0 kHz), 14 (kx 27, 10.1 kHz).
Control: FAAC_SBR_START=15 = base, decoded PCM 49/49 at 32k and 48k (checked: 49/49 both).
Score: 49 clips, serial zimtohrli, bits-adjusted vs the knob-unset base with FAAC's own HE slope
(28/40k at 32k, 40/56k at 48k).
Pass: mean adj >= +0.005, W > L (|d| > 0.0005), no clip < -0.05, total bytes within +-12.5 %, the chosen value at the
bracket centre (both neighbouring start values lower; 15 counts as a neighbour), and the same value not worse
(mean adj >= 0) at the other rate. If the best arm sits at an edge of the tried set, extend by one value before
deciding. Apple HE 32k refs: reported per arm (raw MOS, bytes, bits-adjusted with the 28/40 slope); not part of the rule.

## B2a rSF decomposition (2026-09-29 ~21:45 UTC, before building or scoring the arms)
Base as H1, LC (`--object-type lc`), 96k (ref apple_lc96k, slope 80/112) and 128k (ref apple, slope 112/144); step1,
no rate loop. Builder `scripts/s4/b2_make.py` on the d_make rSFr band set (regular in both channels of both streams,
same layout). Arms vs F, bits-adjusted: rSFr (reference), rSFlev (FAAC shape at Apple's per-ICS mean level), rSFshape
(Apple shape at FAAC's per-ICS mean level), rSFr0..3 (Apple sf only in 0-2 / 2-6 / 6-12 / >12 kHz).
Controls first: Control 0, KA, KF 49/49 (as S3), and K0 (builder, no change) PCM-identical to KF on 49/49.
Reading: a part "carries" rSF if its mean adj >= +0.005, W > L, and it is >= 50 % of rSFr at both 96k and 128k.
If lev carries: the lever is rate-loop / reservoir distribution across frames; if shape: the per-band masking curve;
if one region carries: that region's allocation. If none carries (the gain needs level and shape together, or is
spread), say so and screen nothing. An encoder knob for the carrying lever is then screened with the H1 rule
(rate loop on, 49 clips, >= +0.005, W > L, no clip < -0.05, bytes +-12.5 %, bracket centre, 96k and 128k not worse).

## H1 amendment (2026-09-29, after the first pass, before re-scoring st8 and scoring the added arms)
Arm st8 (kx 16) as first scored is INVALID: pick_stop_freq() starts its search at bs_stop_freq 10, whose k2 (49 at
48 kHz) already exceeds the k2 - kx <= 32 span decoders accept; ffmpeg and FAAD drop the SBR ("too many QMF
subbands: 33") and output a 6 kHz lowpass (velvet 2.10 vs 3.63). Fixed in the probe (search from 0; kx 16 -> k2 45;
new binary 49/49 PCM-identical to the old one at 32k and 48k with the knob unset and at start 11). st8 is re-scored
with the fixed binary. Added arms, same rule: st7 at 32k (kx 15 = fdk-aac's 32k crossover) and st12 at 48k
(kx 22 = fdk-aac's 48k crossover, between st11 and st13).
