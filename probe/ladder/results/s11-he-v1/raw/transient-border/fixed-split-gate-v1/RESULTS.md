# Fixed transient-border split gate

This four-clip, four-rate screen compares eight unconditional fixed interior-border placements (2, 4, 6, or 8) against matched upstream and retained-stack references. It is an attribution screen, not a full-corpus or production result. Negative BD-rate means fewer bits for equal fitted MOS. The adaptive comparisons reuse the frozen mode-1 gate rows on the same clips and rates; they are paired screening comparisons only.

All 128 candidate encodes were forced HE-AAC v1 and strict FDK-decoded before MOS. All 160 rows passed FAAD3 and FFmpeg. Exact reference packet/PCM identities were checked before scoring; MOS reuse occurred only on identical packet and decoded PCM. The candidate environment contained no inherited `FAAC_SBR_*` values.

| Fixed split | Context | Mean BD-rate vs matched reference | Four-clip bootstrap 95% CI |
|---:|---|---:|---:|
| 2 | base | +1.661% | [+0.180%, +2.956%] |
| 2 | stack | +1.448% | [+0.521%, +2.014%] |
| 4 | base | +0.531% | [+0.280%, +0.840%] |
| 4 | stack | +0.460% | [-0.433%, +1.406%] |
| 6 | base | +0.183% | [-0.883%, +1.385%] |
| 6 | stack | +0.159% | [-1.052%, +1.744%] |
| 8 | base | -0.443% | [-0.857%, -0.138%] |
| 8 | stack | -0.813% | [-1.340%, -0.423%] |

## Adaptive mode 1 versus fixed placements

Paired BD-rate is adaptive relative to the fixed candidate; negative values favor adaptive border placement. Each interval resamples the same four clips.

| Adaptive setting | Fixed comparator | Mean BD-rate | 95% CI |
|---|---:|---:|---:|
| mode1_lam0001_base | fixed2_base | -2.330% | [-4.736%, +0.452%] |
| mode1_lam0001_base | fixed4_base | -1.270% | [-2.342%, +0.099%] |
| mode1_lam0001_base | fixed6_base | -0.927% | [-1.267%, -0.587%] |
| mode1_lam0001_base | fixed8_base | -0.304% | [-1.083%, +0.815%] |
| mode1_lam0001_stack | fixed2_stack | -2.527% | [-4.029%, -0.956%] |
| mode1_lam0001_stack | fixed4_stack | -1.578% | [-2.051%, -1.105%] |
| mode1_lam0001_stack | fixed6_stack | -1.278% | [-2.177%, -0.328%] |
| mode1_lam0001_stack | fixed8_stack | -0.324% | [-0.907%, +0.259%] |
| mode1_lam0003_base | fixed2_base | -2.332% | [-4.867%, +0.493%] |
| mode1_lam0003_base | fixed4_base | -1.274% | [-2.437%, +0.101%] |
| mode1_lam0003_base | fixed6_base | -0.931% | [-1.275%, -0.587%] |
| mode1_lam0003_base | fixed8_base | -0.308% | [-1.216%, +0.855%] |
| mode1_lam0003_stack | fixed2_stack | -2.421% | [-4.034%, -0.848%] |
| mode1_lam0003_stack | fixed4_stack | -1.476% | [-2.060%, -0.893%] |
| mode1_lam0003_stack | fixed6_stack | -1.178% | [-2.071%, -0.334%] |
| mode1_lam0003_stack | fixed8_stack | -0.223% | [-0.743%, +0.259%] |

The adaptive selector outperformed fixed-6 in these four-clip comparisons in both base and stack contexts (all four intervals exclude zero). Against fixed-4, both base intervals cross zero while both stack intervals favor adaptive. Against fixed-8, all intervals cross zero. Fixed-8 itself improved fitted quality against both matched references on this gate, so the gate does not establish a general advantage for adaptive placement over the best tested fixed setting. This is screen-level attribution only; full-corpus follow-up requires root review.

See `config.json`, `rows.json`, `summary.json`, `adaptive-attribution.json`, and `interop-results.json` for provenance and per-clip/per-rate values. Fixed variant build manifests with source/binary/library hashes are under `/private/tmp/faac-border-fixed{2,4,6,8}-{base,stack}/probe/variant.json`; the validation smoke artifact is `/private/tmp/faac-border-fixed-controls-validation.json`.
