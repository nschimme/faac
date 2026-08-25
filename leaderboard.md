# AAC Encoder Leaderboard

## Overall Rankings

| Rank | Encoder | Status | Avg MOS | Worst MOS | Stereo Fidelity | Speed (xRT) | Bitrate Error | ROM (Flash) |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| 🏆 1 | FAAC | OK | **4.273** | 4.135 | **0.9712** | **168.6x** | **8.8%** | 72.4 KB |

## Per-Scenario Quality (MOS)

| Scenario | FAAC |
| :--- | :---: |
| 48k_stereo_32k | **4.241** |
| 48k_stereo_48k | **4.276** |
| 48k_stereo_64k | **4.301** |

## Per-Scenario Stereo Fidelity

> **Note**: Measured as 1.0 - |Coherence(Ref) - Coherence(Deg)|. **Higher is truer** (closer to reference stereo image).

| Scenario | FAAC |
| :--- | :---: |
| 48k_stereo_32k | **0.9575** |
| 48k_stereo_48k | **0.9754** |
| 48k_stereo_64k | **0.9806** |

## Per-Scenario Bitrate Accuracy (Error %)

| Scenario | FAAC |
| :--- | :---: |
| 48k_stereo_32k | **8.6%** |
| 48k_stereo_48k | **9.7%** |
| 48k_stereo_64k | **8.2%** |

## Per-Scenario Efficiency (Speed xRT)

| Scenario | FAAC |
| :--- | :---: |
| 48k_stereo_32k | **88.4x** |
| 48k_stereo_48k | **219.0x** |
| 48k_stereo_64k | **198.3x** |

## Failure Analysis

| Encoder: Error Type | Occurrences |
| :--- | :---: |
| FFmpeg AAC: Encoding failed | 12 |

---
**Metric Legend**:
- **Avg MOS**: Perceptual quality (1-5, **Higher is Better**)
- **Stereo Fidelity**: Faithfulness of stereo image (0-1, **Higher is Better**)
- **Speed**: Encoding throughput (**Higher is Better**)
- **Bitrate Error**: Absolute deviation from target bitrate (**Lower is Better**)
- **ROM (Flash)**: Exact compiled executable code and read-only data size (.text + .rodata) inside the codec library/binary (**Lower is Better**)
