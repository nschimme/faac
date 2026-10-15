# Fixed-four production control quality risk

The unconditional steady four-envelope production builds are packet- and strict FDK PCM-identical to the scored `all_base` and `all_stack` probe rows over all 196 clip/rate pairs. The per-rate paired MOS and packet-bitrate differences are archived in `quality-risk.json`; negative MOS means a lower metric score than the matched reference.

At 32 kbps, all4-base versus frozen baseline had mean MOS delta +0.01942 (95% clip-bootstrap CI +0.00295 to +0.03778) and mean AAC payload delta +0.0659 kbps. Its largest MOS drops were `bas.wav` (-0.1392 MOS, +0.0544 kbps), `Can't Wait Until Tonight (Dry Wurlitzer Mix)` (-0.0730, +0.0368 kbps), and `sandman` (-0.0631, +0.0200 kbps). All4-stack versus the retained stack had mean MOS delta +0.01596 (CI -0.00002 to +0.03347) and the same mean payload delta; largest drops were `bas.wav` (-0.1409, +0.0544 kbps), the Wurlitzer clip (-0.0773, +0.0368 kbps), and `sandman` (-0.0708, +0.0200 kbps).

At 48/64/96 kbps, the all4-stack mean MOS deltas were +0.01368 [95% CI +0.00698,+0.02025], +0.01633 [+0.01147,+0.02145], and +0.02115 [+0.01626,+0.02679]. The all4-base means were +0.01431 [+0.00753,+0.02072], +0.01783 [+0.01250,+0.02336], and +0.02107 [+0.01625,+0.02685].

These fixed-four rows improve full-curve BD-rate versus their references, while the fixed-four 32 kbps outliers show localized MOS decreases. Interpret the average and clip-level tails together. See the full matched curve results in `../full-active-lambda/RESULTS.md` and the raw scored rows in `../full-active-lambda/envcount-results.json`.
