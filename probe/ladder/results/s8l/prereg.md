# S8-L preregistration (before any S8-L arm score)

Base: static probe on this branch, LC 128k and 96k, `FAAC_SF_SMOOTH=0.6 FAAC_BS_DROPRATIO=12`. Apple references are `ref/apple` and `ref/apple_lc96k`. Per-clip bits adjustment uses the same-base 112/144 and 80/112 slope encodes. Decisions require all 49 indexed clips.

## Existing ceiling and controls

S4-B2 already measured this LC configuration: `LADDER_RESULT.md` Stage S4-B2 says the two knobs above were used, and `results/s4/b2_128k.json` gives rSFr +0.0153 (40/8), rSFr0 +0.0072, rSFr1 +0.0066; at 96k rSFr +0.0113. The S4-B2 result commit is `5fd19b98`. From that commit to this branch, `git diff 5fd19b98 HEAD -- libfaac/` changes only `quantize.c` PNS probe knobs (unset thresholds preserve the old tests) and `sbr_bitstream.c` (HE only). S7-STACK also reports LC streams identical to S5-F2 on 43–47/49; that is corroboration, not a 49/49 identity proof. Thus the old-base rSFr reproduction control is skipped because the LC encoder configuration is unchanged. The S4-B2 number is the stack-base ceiling, subject to the fresh K0 check below.

K0, the builder with no change, must decode PCM-identical to the base normal encode on 49/49 clips at each rate before any score is read. Any failure blocks interpretation. The fresh rSFr/rSFr0/rSFr1 arms check the transfer locally; compare them with S4-B2, but do not reinterpret a subset as a decision.

## Decision rule

If stack-base rSFr is below +0.007 adjusted MOS at 128k, the LC 128k residual is outside the scalefactors. Stop scalefactor work and report the S3 decision-swap ceilings for M/S, windows, bandwidth, and TNS, one at a time on this base.

If stack-base rSFr is at least +0.007, characterize Apple's scalefactor differences by band, tonality and energy, and masked versus bit-limited conditions. Test at most two simple encoder rules as probe environment knobs. Each knob unset must be PCM-identical on 49/49. Bracket at least three values plus neutral. A rule passes only when mean adjusted MOS is at least +0.005 at 128k, W > L, no clip below −0.05, bytes within ±12.5%, its selected value is the bracket centre, and 96k does not worsen by more than 0.005.

Do not repeat the §5 dead ends: constant offsets, level-only, shape-only, or feature fits. Compare decoded PCM for controls; MP4 metadata may differ. Flag per-clip byte ratios beyond ±12.5%. The S4-B2 result already meets the +0.007 branch, so the characterization branch is the expected follow-up after the full controls.
