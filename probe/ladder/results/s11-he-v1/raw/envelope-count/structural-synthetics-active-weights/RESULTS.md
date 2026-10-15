# Adaptive envelope-count structural sweep (no MOS)

Probe source HEAD: `99ce2b0980180194f9ccd1993a786208d277cf3e`; tracked/untracked source-tree SHA-256: `e10f50f56974f909f771561a6fc54e29c9e1f5fb3ef089138a5ffb003c645643`.
Every input was encoded at 32 kbps in HE-AAC v1, strict-FDK decoded, and tested under both unset controls. No quality metric was computed.
D is weighted independent quantized-envelope error before decoder patch/filter/smoothing and stereo coupling level/balance remapping. It is a surrogate, not decoded waveform distortion or exact rate-distortion. Exact dry-writer bits include the minimum coupled/uncoupled layouts and header/fill rounding.

| Adaptive setting | Context | Active rows | Selected counts | Initial mode-0 | No alternatives | Transient 2 | Retries |
|---|---|---:|---|---:|---:|---:|---:|
| tier_base | base | 1134 | 1: 1134 | 6 | 1134 | 0 | 9 |
| all_base | base | 1134 | 4: 1134 | 6 | 1134 | 0 | 10 |
| ad0005_base | base | 1134 | 1: 862, 2: 86, 4: 186 | 6 | 0 | 86 | 8 |
| ad001_base | base | 1134 | 1: 928, 2: 110, 4: 96 | 6 | 0 | 110 | 9 |
| ad003_base | base | 1134 | 1: 923, 2: 192, 4: 19 | 6 | 0 | 192 | 9 |
| tier_stack | stack | 1134 | 1: 1134 | 6 | 1134 | 0 | 9 |
| all_stack | stack | 1134 | 4: 1134 | 6 | 1134 | 0 | 10 |
| ad0005_stack | stack | 1134 | 1: 862, 2: 86, 4: 186 | 6 | 0 | 86 | 8 |
| ad001_stack | stack | 1134 | 1: 928, 2: 110, 4: 96 | 6 | 0 | 110 | 9 |
| ad003_stack | stack | 1134 | 1: 923, 2: 192, 4: 19 | 6 | 0 | 192 | 9 |

CBR busy-AM retries: 90 rows; all stable: `True`.
Adaptive count 2 activated: `True`; count 4 activated: `True`.
State snapshot assertion present and exercised: `True`.
The earlier lambda .01/.03/.1 structural sweep is archived separately as inactive (all selected count 1); it has no MOS result.
