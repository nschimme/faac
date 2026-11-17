# S12 summary: FAAC master (3a656f7) vs Apple AAC and FDK, 48 kHz first

77 full-length clips of faac-benchmark 9f5f102; packaged Apple/FDK/libxaac streams (`ref/*-48k`); FAAD2, FFmpeg,
fdkdec; realized bitrate; worst clip x rung first. Rebuilt WAVs match 0/77 manifest hashes (same audio at lag 0, 41
clips +1 sample); every headline is also given on the 36 length-matched clips (`stage0/`). Raw rows are committed next
to each RESULTS file. Protocol addenda: `PROTOCOL_ADDENDUM.md`; running table: `SCOREBOARD.md`.

## Baselines (stage 1, `stage1-rebaseline/RESULTS.md`)
- Local scores reproduce the leaderboard within 0.001 MOS (FAAC, Apple, FDK). The leaderboard runs FAAC in CBR.
- LC 128-320: FAAC leads FDK by 14-23 % BD-rate. vs Apple: ABR leads in 128-192 and 128-320 (-2.5..-2.8 %, both
  methods), 192-320 is parity (method-dependent); CBR is unresolved everywhere (about -1 %). At equal realized rate
  FAAC trails Apple only at 192k (-0.0027, 61/77 clips) and 320k (-0.0012).
- HE-v1 32-64: FAAC leads Apple (-8 %), FDK (-5.5 %), libxaac (-10 %) in BD-rate and mean MOS; P1 trails FDK/xaac at
  40/48k by 0.05-0.14 (losers are mostly stereo-fidelity clips); stereo fidelity trails at 32/40k (FAAC uses IS, no M/S).
- HE-v2: parity with Apple/FDK; vs libxaac unresolved.
- Pre-echo (NPER, pooled as the benchmark pools it) is the largest gap: HE-v1 21.1/18.4/16.5 dB vs FDK 15.4/14.8/14.4;
  LC 128k 5.12 vs Apple 3.85. The pooled metric is dominated by 13 fatboy (44 % of events); per-clip the HE gap is in
  6-12 kHz.

## Items
| item | verdict | evidence |
|---|---|---|
| HE-v2 collapse >= 192k | not on master: flat MOS 96-320; master caps HE-v2 ABR at ~107 kbps; gist build a7d2f755 not found | `anomaly-hev2-high/` |
| Intensity stereo screen | not run; at >= 160 kbps master uses no IS at all (code + dumps), lever spent | `stage2-intensity/PREREG.md`, `decisions/stereo_shares.md` |
| CBR aim (item 1) | v1 midpoint PASSED LC 48 kHz CBR (-0.72 % vs master) but FAILED CI (PR #208: low-rate mono speech / low-rate stereo cells past -0.10); v2 one-sided: see below | `item1-cbr/` |
| Short-window TNS v1 | closed: NPER -1..-3.8 dB but MOS gate fails | `stage3-transients/RESULTS_stns_v1_lc.md` |
| Short-window TNS v2 (attack window) | closed: MOS gate fails, keeps 14-46 % of v1's NPER gain | `stage3-transients/RESULTS_stns_v2_lc.md` |
| SBR attack-aligned border (simple) | closed at mechanism check (borders not nearer onsets) | `stage3-transients/PREREG_sbr_attack.md` |
| SBR onset-adaptive grid | pre-scoring gates passed (86-91 % of onsets within 2 slots); +2396 B footprint; HE screen: see below | `stage3-transients/PREREG_sbr_grid.md`, `scripts/s12/SBRGRID_VALIDATION.md` |
| Lower HE crossover, PNS gating, LC block-switch, cutoff, HE --joint | not run (closed by history or deferred by the owner's delivery-mode instruction) | `decisions/he32_core_onsets.md`, `item1-192-320/` |

## Open at stop
- CBR v2 and SBR grid HE screen verdicts are recorded below when they land.
