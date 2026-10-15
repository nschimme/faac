# Cross-frame border four-clip gate results

This is a screening gate only: four clips, 32/48/64/96 kbps, with matched baseline and retained-stack references. It does not establish a full-corpus or production result. Negative BD-rate means fewer bits for equal fitted MOS. Bootstrap intervals resample the four clips; the 16 feature configurations are a screening matrix, so the single interval excluding zero is not a post-selection confirmation.

All 320 streams passed strict FAAD3 and FFmpeg. The 32 baseline/stack reference and mode-off control rows were verified before scoring; controls matched their packet and strict FDK-decoded PCM identities. Every scored stream was verified as HE-AAC v1 and decoded by strict FDK before Zimtohrli MOS scoring.

The two future-PCM settings implement distinct detector policies: future_pcm=0 compares delayed-tail q0..5 against sampled current-N highband mean; future_pcm=1 compares actual q6..11 mean against delayed-tail q0..5. They are not a pure future-data ablation. Mode 1 caps the extension differently from mode 2. No forced-extension or trace flags were used in quality environments.

| Setting | Matched context | Mean BD-rate | Clip-bootstrap 95% CI | Per-clip BD-rate |
|---|---|---:|---:|---|
| mode1/f0 λ=.001 base | matched reference | -1.011% | [-2.673%, +0.735%] | sandman: -3.265%, velvet: -0.895%, 21-classic.441: +1.411%, fms.wav: -1.294% |
| mode1/f0 λ=.001 stack | matched reference | -1.373% | [-3.303%, +0.063%] | sandman: -4.166%, velvet: -0.713%, 21-classic.441: +0.432%, fms.wav: -1.045% |
| mode1/f0 λ=.003 base | matched reference | -1.292% | [-3.056%, +0.025%] | sandman: -3.812%, velvet: -0.906%, 21-classic.441: +0.335%, fms.wav: -0.786% |
| mode1/f0 λ=.003 stack | matched reference | -1.421% | [-3.479%, +0.154%] | sandman: -4.399%, velvet: -0.717%, 21-classic.441: +0.590%, fms.wav: -1.155% |
| mode1/f1 λ=.001 base | matched reference | -0.924% | [-2.158%, +0.234%] | sandman: -2.903%, velvet: +0.077%, 21-classic.441: +0.390%, fms.wav: -1.261% |
| mode1/f1 λ=.001 stack | matched reference | -1.171% | [-2.970%, +0.343%] | sandman: -3.900%, velvet: +0.864%, 21-classic.441: -0.179%, fms.wav: -1.470% |
| mode1/f1 λ=.003 base | matched reference | -0.295% | [-1.635%, +1.094%] | sandman: -1.631%, velvet: +0.087%, 21-classic.441: +2.003%, fms.wav: -1.639% |
| mode1/f1 λ=.003 stack | matched reference | -0.738% | [-2.216%, +0.740%] | sandman: -2.822%, velvet: +0.870%, 21-classic.441: +0.610%, fms.wav: -1.611% |
| mode2/f0 λ=.001 base | matched reference | -1.135% | [-2.870%, +0.614%] | sandman: -3.527%, velvet: -0.899%, 21-classic.441: +1.284%, fms.wav: -1.397% |
| mode2/f0 λ=.001 stack | matched reference | -1.431% | [-3.689%, +0.378%] | sandman: -4.644%, velvet: -0.826%, 21-classic.441: +0.884%, fms.wav: -1.140% |
| mode2/f0 λ=.003 base | matched reference | -1.540% | [-3.089%, -0.153%] | sandman: -3.814%, velvet: -0.914%, 21-classic.441: +0.411%, fms.wav: -1.845% |
| mode2/f0 λ=.003 stack | matched reference | -1.470% | [-3.415%, +0.057%] | sandman: -4.269%, velvet: -0.852%, 21-classic.441: +0.494%, fms.wav: -1.254% |
| mode2/f1 λ=.001 base | matched reference | -0.456% | [-2.229%, +1.698%] | sandman: -2.838%, velvet: +2.689%, 21-classic.441: -0.402%, fms.wav: -1.274% |
| mode2/f1 λ=.001 stack | matched reference | -0.879% | [-2.479%, +0.720%] | sandman: -3.413%, velvet: +0.803%, 21-classic.441: +0.636%, fms.wav: -1.544% |
| mode2/f1 λ=.003 base | matched reference | -0.123% | [-1.770%, +1.675%] | sandman: -2.202%, velvet: +2.679%, 21-classic.441: +0.368%, fms.wav: -1.338% |
| mode2/f1 λ=.003 stack | matched reference | -0.433% | [-1.976%, +1.110%] | sandman: -2.391%, velvet: +0.825%, 21-classic.441: +1.396%, fms.wav: -1.560% |

Only mode2, future_pcm=0, λ=.003 against base has a gate CI excluding zero (−1.540%, 95% CI [−3.089%, −0.153%]); the remaining 15 intervals cross zero. Given the number of screened settings and four-clip sample, this is a candidate for root review rather than a validated winner. Across most settings the strongest clip gain is sandman, while 21-classic or velvet often regresses; the per-clip values are listed above.

See `config.json`, `rows.json`, `summary.json`, and `interop-results.json` for the immutable run configuration and raw evidence. Source HEAD is `99ce2b0980180194f9ccd1993a786208d277cf3e`; probe patch SHA-256 is `f3e3c10c89c8762d307c4e7f0cd8474a50f07021a3a3285e5167b381fbf864d2`.
