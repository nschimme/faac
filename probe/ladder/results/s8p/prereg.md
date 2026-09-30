# Stage S8-P Pre-Registration (PNS Threshold at HE)

Date: 2026-09-30

## Objective
Investigate the PNS threshold (`FAAC_PNS_THRESH`) behavior at HE 48k and HE 32k across the sweep 0.2, 0.3, 0.35, 0.4 (= base F), 0.45, 0.5. Settle the non-monotonic result reported in Stage S7-CORE where both 0.3 and 0.5 reported MOS wins over default.

## Encoder Base (Base F)
- Static build of libfaac from this branch.
- Base environment variables:
  `FAAC_SF_SMOOTH=0.6`
  `FAAC_BS_DROPRATIO=12`
  `FAAC_SBR_FREQ_SCALE=3`
- Rungs: HE ABR 48k and HE ABR 32k, 48 kHz stereo, 49 corpus clips.
- Bits-adjustment per clip using FAAC's own slope pairs:
  - HE 48k slope anchors: 40 kbps / 56 kbps
  - HE 32k slope anchors: 28 kbps / 40 kbps

## Pre-Registered Controls
1. **Control C1 (Threshold Equivalence):**
   - Setting `FAAC_PNS_THRESH=0.4` must produce 100% identical decoded PCM to base F (`FAAC_PNS_THRESH` unset) on 49/49 clips at HE 48k and HE 32k.
2. **Control C2 (Scorer Determinism):**
   - Re-score 5 clips twice with `score_clip.py` (zimtohrli) to confirm 100% identical MOS.
3. **Control C3 (PNS Monotonicity & Visibility):**
   - Report the PNS band share (long blocks, and short blocks separately) per arm from FAAD dumps or encoder stats to confirm the knob is active and monotonic with respect to threshold value.

## Pre-Registered Arms & Sweeps
- **Initial Arms:** `FAAC_PNS_THRESH` = 0.2, 0.3, 0.35, 0.4 (= F), 0.45, 0.5 at HE 48k and HE 32k.
- **Extension Rule:** If the best performing value is at an edge (0.2 or 0.5), extend the sweep by one step beyond it (e.g., 0.1 or 0.55/0.6).

## Pre-Registered Decision Criteria for Candidate Selection
A value passes and is selected as a candidate if:
1. **HE 48k Adjusted Mean MOS:** $\ge +0.005$ vs base F.
2. **Wins vs Losses at HE 48k:** $W > L$.
3. **Worst Clip Delta at HE 48k:** No clip delta $< -0.05$.
4. **Byte Ratio:** Mean byte ratio within $\pm 12.5\%$ ($0.875 \le \text{ratio} \le 1.125$).
5. **Bracket Centre:** Both immediate neighbors in the sweep perform worse than the candidate.
6. **HE 32k Check:** HE 32k adjusted mean MOS is not worse than base F by more than $0.005$ with $W \ge L$.

## Promotion & Production Change Policy
- If a value passes all criteria:
  - Run the winning threshold at LC 96k and LC 128k (without forcing `--object-type lc`) to check if an HE-only gate is required.
  - Implement the change in `libfaac/frame.c` or `libfaac/quantize.c` (HE-only if LC regresses).
  - Commit the candidate in its own commit with subject `quantize: <imperative summary>` and a descriptive "why" paragraph.
- If non-monotonic behavior persists (e.g. both 0.3 and 0.5 win):
  - Analyze per-clip overlap between the 0.3 and 0.5 winners, noise vs signal tradeoffs, and which specific bands change to explain the perceptual behavior.
