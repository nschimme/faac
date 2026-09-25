# Cumulative quality measurement

The fixed baseline is pre-stack upstream `95e6c483a77e931884a2c533a41c05e4dbf632ac`.
Do not add gains measured against different baselines. The combined encoder
contains the current upstream #228 and #229 heads and the complete #562 work,
which is already submitted upstream as #230.

| Stage | Upstream/source head | Clean measurement commit |
| --- | --- | --- |
| Original baseline | 95e6c483 | 95e6c483 |
| #228 | 774eb19e | c45f25e2 |
| #228 + #229 | #229 2a56a739 | c2c9a936 |
| Full stack | #562 d00bcaf3; #230 f9151d1f | 44959a06 |

The fork CI branch has the same encoder source as `44959a06`, with the fork's
benchmark workflow retained. Its benchmark and footprint baseline are pinned
to `95e6c483`, and the benchmark harness to
`916be7905213913a30b112a4c13f2547a47942f4`. ABR/CBR/VBR matrix jobs run serially.
Fresh throughput is measured when quality results come from the baseline cache.

The completed #562 run 36076211846 reports +0.014 average MOS for both ABR and
CBR and roughly 2.2% fewer VBR bytes. Its aggregate report fails BD-rate, MOS
and throughput gates, including a -0.32 MOS outlier at HE24. The reported
throughput change is -9.2% overall, with larger losses on LC. Those figures
use fork master `693654ad`, not a measured cumulative combination. They are
not entered as additional gains in this ledger.

## Measurement protocol

`tools/cumulative_bench.py` records all 49 original 48 kHz stereo clips at
forced HE40/48/64 and LC96/128/160/192. Both FFmpeg and FDK decode each stream.
Both ViSQOL and Zimtohrli are recorded explicitly. Earlier diagnostic scripts
labelled their scores ViSQOL even though their current audio backend is
Zimtohrli; their old labels are not reused here.

Each run records source revisions, tracked diffs, build options, binary/library
hashes, decoder/tool versions, metric package versions, corpus hashes, exact
commands and probe environment. `ffprobe` checks every output profile. Saved
rows permit resumption only under the identical manifest. Raw and AAC payload
bytes are retained separately; byte adjustment uses payload bytes.

`tools/gain_ledger.py` derives per-clip slopes from adjacent rungs of the same
profile in the fixed baseline's ladder. The slope is the central difference
of MOS against log2(payload bytes); endpoint rungs use a one-sided difference.
Negative slopes are retained. Cumulative and marginal deltas use this same
calibration, so they telescope. Remaining Apple/FDK gaps use the full stack's
own ladder and are reported separately. Wins/losses use a ±0.02 MOS threshold.

Stereo coherence uses time-aligned decoded/reference stereo, with silent
frames excluded; high-band coherence applies a 1.5 kHz high-pass before the
same calculation. This is a new consistent measurement, not a continuation
of old unaligned coherence values.

`tools/section_attribution.py` generates fresh dumps using the decoder with
committed M/S correction `486d97ba`. It accounts for long and grouped-short
windows separately, preserving SF chains across groups and ending section
runs at group boundaries. Coverage, sections, class transitions, regular-book
breaks, run-length escapes and SF bits by class are recorded at HE48/LC128.

The joint spectral/scalefactor RD probe is maintained on a separate experimental
branch. It is default-off and is not included in this integration PR. Its
promotion gate is +0.03 bits-adjusted MOS on all 49 clips at HE48 or LC96 with
both decoders, corroborating Zimtohrli and no aggregate coherence regression.

The approximately 36 kbps/channel automatic HE/LC crossover remains queued
until #228 lands. LC block-switch ratio 4 is measured separately on the
original baseline and then on the cumulative stack before any recommendation.

## Results

Full local measurements and the integration CI are in progress. No cumulative
+0.1 MOS claim or probe promotion is made before those measurements complete.
