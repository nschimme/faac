# HE-AAC v1 adaptive envelope-count experiment

Phase: full; 49 clips at 32, 48, 64, 96 kbps.
Every stream forces HE-AAC v1, is independently profile-checked, FDK-decoded before MOS scoring, and validated by strict FAAD3 and FFmpeg after scoring.
Actual bitrate uses AAC packet bytes divided by source duration; BD-rate uses the benchmark PCHIP helper and clip-bootstrap intervals.

| Variant | Matched reference | Mean BD-rate (95% CI) | Median | Packet ties | PCM ties |
|---|---|---:|---:|---:|---:|
| tier_base | baseline_original | -0.347% [-0.409%, -0.289%] | -0.310% | 147/196 | 147/196 |
| all_base | baseline_original | -2.043% [-2.784%, -1.322%] | -1.599% | 0/196 | 0/196 |
| ad001_base | baseline_original | -2.040% [-2.744%, -1.391%] | -1.496% | 0/196 | 0/196 |
| ad003_base | baseline_original | -2.271% [-2.888%, -1.692%] | -1.684% | 0/196 | 0/196 |
| tier_stack | stack_reference | -0.348% [-0.410%, -0.290%] | -0.308% | 147/196 | 147/196 |
| all_stack | stack_reference | -1.862% [-2.569%, -1.178%] | -1.432% | 0/196 | 0/196 |
| ad001_stack | stack_reference | -1.738% [-2.403%, -1.094%] | -1.309% | 0/196 | 0/196 |
| ad003_stack | stack_reference | -1.913% [-2.540%, -1.287%] | -1.373% | 0/196 | 0/196 |

Post-score decoder validation: FAAD3 2352/2352; FFmpeg 2352/2352.
Cached MOS is reused only after AAC packet and strict FDK-decoded PCM hashes match the prior measured stream.
No winner or production claim is implied by a gate run.
