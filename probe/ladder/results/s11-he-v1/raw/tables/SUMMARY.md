# Frequency-table policy screen

Four gate clips, four rates (32/48/64/96 kbps), frozen upstream encoder base99ce2b09. Every unset control matched baseline AAC packets and strict FDK PCM (16/16). Every score followed strict FDK decoding and forced HE profile verification. FAAD3 and FFmpeg each accepted all 112 scored/control streams.

| One-factor setting | Mean BD-rate vs baseline | 95% clip-bootstrap CI |
|---|---:|---:|
| Frequency scale1 | +0.853% | [+0.086%, +1.558%] |
| Frequency scale2 | +0.820% | [+0.364%, +1.276%] |
| Frequency scale3 | 0% | [0%, 0%] |
| Stop target16kHz | +2.477% | [+0.191%, +6.422%] |
| Stop target18kHz | +0.660% | [-0.098%, +1.498%] |

No setting demonstrated a screening gain; scale3 reproduced baseline exactly. The default rate policy already used scale3 at all tested rates. The other scales and16kHz ceiling regressed on the gate;18kHz remains inconclusive and is not promoted. No full-corpus, timing, or production footprint claim is made for this family. This result tests the bounded settings, not every possible frequency-table policy.

See gate/RESULTS.md, gate/tables-config.json, gate/tables-results.json and gate/interop-results.json; the exact probe is tables-probe.patch and its runner is run_tables.py. Source was frozen during measurement.
