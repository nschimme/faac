# Frequency-local SBR noise/INVF probe

This is a probe-only encoder implementation based on `upstream/master` `99ce2b09`, isolated in `/private/tmp/faac-he-v1-localnoise` on branch `he-v1-localnoise`. No original tracked source files were changed. The implementation uses two frequency-local noise bands, per-channel/per-band tonality and INVF decisions, adaptive noise levels, and noise frequency/time Huffman coding with coupled level/balance handling. The source patch and runner are archived here; encoded artifacts remain under `/private/tmp/faac-he-v1-localnoise/probe/results-all/encodes`.

The encoder derives noise-band boundaries from its high/low frequency tables. At all tested 48 kHz rates, selector 1 produced `nq=1`, edges `[31,54]`; selector 2 produced `nq=2`, edges `[31,37,54]`. Full results contain 1,176 variant rows over 49 clips and 32/48/64/96 kbps, including 392 one-band static-control rows whose MOS was reused by packet/PCM identity; four entries are new two-band variants. Baseline and candidate controls are also present. The frozen unset probe matched the baseline for all 16 gate AAC packet hashes.

| Candidate vs reference | Mean BD-rate (95% clip-bootstrap CI) | Worst clip |
|---|---:|---|
| Two-band static 18 vs one-band static 18 | +0.401% [+0.172%, +0.622%] | `hrp.wav` (+2.554%) |
| Two-band static 24 vs one-band static 24 | +0.403% [+0.179%, +0.622%] | `hrp.wav` (+2.535%) |
| Two-band adaptive 18 vs one-band static 18 | +0.355% [+0.109%, +0.591%] | `9-Have-big-expensive-car.441.16b48k.wav` (+2.596%) |
| Two-band joint 18 vs one-band static 18 | +0.590% [+0.018%, +1.372%] | `glk.wav` (+13.882%) |

Positive BD-rate is worse. The two-band adaptive candidate also measured +0.374% [+0.138%, +0.602%] versus one-band static 24; joint measured +0.611% [+0.027%, +1.404%] versus static 24. The static two-band comparisons against their one-band counterparts are likewise regressions. The joint candidate's +10.202% worst clip against original baseline is `glk.wav`. These results reject the tested frequency-local variants against their matched one-band static controls; they do not show that every possible multi-band design is worse.

All newly scored rows passed strict FDK before Zimtohrli scoring; the raw rows identify cached MOS reused by exact packet/PCM identity separately. Full-corpus strict in-tree FAAD3 and FFmpeg validation accepted all 1,568 unique streams (including baseline and candidate controls); both reported zero failures. The validator checks stream acceptance, not decoded PCM equality. The earlier four synthetic inputs and gate decoder checks are recorded in `rawresults/`; they do not substitute for quality evidence.

No ROM, timing, RAM, or stack measurement was made because none of these variants won its matched quality comparison. This remains a bounded probe, not a shipping decision. Exact outcomes and per-rate paired MOS/payload deltas are in [full results](full/RESULTS.md) and [direct comparisons](full/direct-comparisons.json); decoder outcomes are in [interop results](full/interop-results.json), with settings and provenance in [configuration](full/localnoise-config.json) and [study configuration](config.json).
