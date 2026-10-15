# HE-AAC v1 adaptive envelope-count experiment

Phase: gate; 4 clips at 32, 48, 64, 96 kbps.
Every stream forces HE-AAC v1, is independently profile-checked, FDK-decoded before MOS scoring, and validated by strict FAAD3 and FFmpeg after scoring.
Actual bitrate uses AAC packet bytes divided by source duration; BD-rate uses the benchmark PCHIP helper and clip-bootstrap intervals.

| Variant | Matched reference | Mean BD-rate (95% CI) | Median | Packet ties | PCM ties |
|---|---|---:|---:|---:|---:|
| tier_base | baseline_original | -0.268% [-0.376%, -0.104%] | -0.332% | 12/16 | 12/16 |
| all_base | baseline_original | -0.329% [-1.835%, +1.261%] | -0.412% | 0/16 | 0/16 |
| ad0005_base | baseline_original | -0.471% [-1.700%, +0.931%] | -0.645% | 0/16 | 0/16 |
| ad001_base | baseline_original | -0.429% [-1.749%, +0.866%] | -0.403% | 0/16 | 0/16 |
| ad003_base | baseline_original | -1.072% [-1.775%, -0.370%] | -1.174% | 0/16 | 0/16 |
| tier_stack | stack_reference | -0.279% [-0.356%, -0.199%] | -0.282% | 12/16 | 12/16 |
| all_stack | stack_reference | -0.684% [-1.328%, -0.041%] | -0.635% | 0/16 | 0/16 |
| ad0005_stack | stack_reference | -0.409% [-1.246%, +0.975%] | -1.039% | 0/16 | 0/16 |
| ad001_stack | stack_reference | -1.080% [-1.585%, -0.392%] | -1.264% | 0/16 | 0/16 |
| ad003_stack | stack_reference | -0.938% [-1.740%, +0.010%] | -1.083% | 0/16 | 0/16 |

Post-score decoder validation: FAAD3 224/224; FFmpeg 224/224.
Cached MOS is reused only after AAC packet and strict FDK-decoded PCM hashes match the prior measured stream.
No winner or production claim is implied by a gate run.
