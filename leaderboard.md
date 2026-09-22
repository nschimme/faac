# AAC Encoder Leaderboard

Quality scores are objective proxy estimates (Zimtohrli/ViSQOL), not blind ABX listening test results.

### Overall Encoder Rankings

> **Note**: Overall MOS averages the scenario set listed below, so absolute values are only comparable between leaderboards built from the same set of scenarios. Relative ranking is unaffected.

| Rank | Encoder | Status | Worst MOS | Overall MOS | Scenarios | Stereo Fidelity | Transient Fidelity | Speed (xRT) | Bitrate Error | Peak RAM | ROM (Flash) |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| 🏆 1 | FAAC 2.1.0 (1c127b8-dirty) | OK | **1.735** | **4.365** | 33/33 | 0.9609 | 0.9167 | **122.8x** | 3.3% | 12.1 MB | 68.6 KB |
| 2 | fdkaac 1.0.0 | OK | 1.570 | 4.264 | 33/33 | **0.9860** | **0.9257** | 53.7x | **1.5%** | 12.0 MB | 558.1 KB |
| 3 | FFmpeg AAC 6.1.1-3ubuntu5 | OK | 1.143 | 3.947 | 33/33 | 0.9679 | 0.8800 | 16.1x | 8.0% | 54.0 MB | 234.0 KB |

<a name="per-scenario-encoder-breakdowns"></a>
<details><summary><b>📊 View Per-Scenario Breakdowns & Visualizations</b></summary>

## Per-Scenario Breakdown & Visualizations

### 16 kHz Mono Speech Quality Across Bitrates

<details><summary><b>View Detailed 16 kHz Mono Speech Average & Worst MOS Tables</b></summary>

#### Per-Scenario Average MOS (16 kHz Mono Speech)

#### LC Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) | fdkaac 1.0.0 | FFmpeg AAC 6.1.1-3ubuntu5 |
| :--- | :---: | :---: | :---: |
| 16k_mono_20k | **4.060** ██████░░ | 3.786 ██████░░ | 3.423 █████░░░ |
| 16k_mono_24k | **4.092** ███████░ | 3.853 ██████░░ | 3.600 ██████░░ |
| 16k_mono_voip_24k | **4.102** ███████░ | 4.052 ██████░░ | 3.945 ██████░░ |

#### Per-Scenario Worst MOS (Min Clip MOS - 16 kHz Mono Speech)

> **Note**: Minimum perceptual MOS score observed across any clip in the scenario. Highlights edge-case clip degradation. A 🐛 names the clip when every other encoder scored ≥0.75 MOS higher on that exact clip -- likely a defect specific to this encoder; see Quality Outliers under Issues Worth Investigating below.

#### LC Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) | fdkaac 1.0.0 | FFmpeg AAC 6.1.1-3ubuntu5 |
| :--- | :---: | :---: | :---: |
| 16k_mono_20k | **3.809** ██████░░ | 3.512 ██████░░ | 3.157 █████░░░ |
| 16k_mono_24k | **3.859** ██████░░ | 3.489 ██████░░ | 3.128 █████░░░ |
| 16k_mono_voip_24k | **3.949** ██████░░ | 3.626 ██████░░ | 3.519 ██████░░ |

</details>

### Transient Fidelity (16 kHz Mono Speech)

> **Note**: Measured as 1 / (1 + mean |attack-centroid-shift| ms) across onsets. **Higher is truer** (attack timing closer to reference).

<details><summary><b>View Detailed Transient Fidelity Table (16 kHz Mono Speech)</b></summary>

#### LC Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) | fdkaac 1.0.0 | FFmpeg AAC 6.1.1-3ubuntu5 |
| :--- | :---: | :---: | :---: |
| 16k_mono_20k | **0.9326** ███████░ | 0.8625 ███████░ | 0.8121 ██████░░ |
| 16k_mono_24k | **0.9271** ███████░ | 0.8854 ███████░ | 0.8267 ███████░ |
| 16k_mono_voip_24k | **0.9137** ███████░ | 0.9114 ███████░ | 0.8424 ███████░ |

</details>

### Bitrate Accuracy (16 kHz Mono Speech)

> **Note**: Deviation from target bitrate calculated from pure elementary stream audio bytes. **Lower is Better**.

<details><summary><b>View Detailed Bitrate Accuracy Table (16 kHz Mono Speech)</b></summary>

#### LC Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) | fdkaac 1.0.0 | FFmpeg AAC 6.1.1-3ubuntu5 |
| :--- | :---: | :---: | :---: |
| 16k_mono_20k | 10.1% | **1.9%** | 30.2% |
| 16k_mono_24k | 17.6% | **1.9%** | 32.5% |
| 16k_mono_voip_24k | 7.9% | **2.1%** | 18.4% |

</details>

### 24 kHz Mono Speech Quality Across Bitrates

<details><summary><b>View Detailed 24 kHz Mono Speech Average & Worst MOS Tables</b></summary>

#### Per-Scenario Average MOS (24 kHz Mono Speech)

#### LC Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) | fdkaac 1.0.0 | FFmpeg AAC 6.1.1-3ubuntu5 |
| :--- | :---: | :---: | :---: |
| 24k_mono_28k | **4.703** ████████ | 4.285 ███████░ | 4.007 ██████░░ |
| 24k_mono_32k | **4.736** ████████ | 4.389 ███████░ | 4.298 ███████░ |

#### Per-Scenario Worst MOS (Min Clip MOS - 24 kHz Mono Speech)

> **Note**: Minimum perceptual MOS score observed across any clip in the scenario. Highlights edge-case clip degradation. A 🐛 names the clip when every other encoder scored ≥0.75 MOS higher on that exact clip -- likely a defect specific to this encoder; see Quality Outliers under Issues Worth Investigating below.

#### LC Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) | fdkaac 1.0.0 | FFmpeg AAC 6.1.1-3ubuntu5 |
| :--- | :---: | :---: | :---: |
| 24k_mono_28k | **4.629** ███████░ | 4.076 ███████░ | 3.748 ██████░░ |
| 24k_mono_32k | **4.684** ███████░ | 4.341 ███████░ | 4.047 ██████░░ |

</details>

### Transient Fidelity (24 kHz Mono Speech)

> **Note**: Measured as 1 / (1 + mean |attack-centroid-shift| ms) across onsets. **Higher is truer** (attack timing closer to reference).

<details><summary><b>View Detailed Transient Fidelity Table (24 kHz Mono Speech)</b></summary>

#### LC Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) | fdkaac 1.0.0 | FFmpeg AAC 6.1.1-3ubuntu5 |
| :--- | :---: | :---: | :---: |
| 24k_mono_28k | **0.9393** ████████ | 0.8902 ███████░ | 0.8646 ███████░ |
| 24k_mono_32k | **0.9489** ████████ | 0.8997 ███████░ | 0.8642 ███████░ |

</details>

### Bitrate Accuracy (24 kHz Mono Speech)

> **Note**: Deviation from target bitrate calculated from pure elementary stream audio bytes. **Lower is Better**.

<details><summary><b>View Detailed Bitrate Accuracy Table (24 kHz Mono Speech)</b></summary>

#### LC Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) | fdkaac 1.0.0 | FFmpeg AAC 6.1.1-3ubuntu5 |
| :--- | :---: | :---: | :---: |
| 24k_mono_28k | 12.1% | **1.3%** | 33.0% |
| 24k_mono_32k | 19.0% | **1.3%** | 33.0% |

</details>

### 32 kHz Stereo Quality Across Bitrates

<details><summary><b>View Detailed 32 kHz Stereo Average & Worst MOS Tables</b></summary>

#### Per-Scenario Average MOS (32 kHz Stereo)

#### LC Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) | fdkaac 1.0.0 | FFmpeg AAC 6.1.1-3ubuntu5 |
| :--- | :---: | :---: | :---: |
| 32k_stereo_16k | **2.101** ███░░░░░ | 1.847 ███░░░░░ | 1.429 ██░░░░░░ |
| 32k_stereo_48k | **4.078** ███████░ | 3.089 █████░░░ | 2.888 █████░░░ |
| 32k_stereo_64k | **4.322** ███████░ | 4.320 ███████░ | 3.837 ██████░░ |
| 32k_stereo_80k | 4.480 ███████░ | **4.609** ███████░ | 4.320 ███████░ |
| 32k_stereo_96k | 4.597 ███████░ | **4.722** ████████ | 4.450 ███████░ |

#### HE-v1 Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) |
| :--- | :---: |
| 32k_stereo_16k | _**1.422**_ ██░░░░░░ * |
| 32k_stereo_48k | _**2.862**_ █████░░░ * |
| 32k_stereo_64k | _**3.470**_ ██████░░ * |
| 32k_stereo_80k | _**3.760**_ ██████░░ * |
| 32k_stereo_96k | _**3.826**_ ██████░░ * |

#### Per-Scenario Worst MOS (Min Clip MOS - 32 kHz Stereo)

> **Note**: Minimum perceptual MOS score observed across any clip in the scenario. Highlights edge-case clip degradation. A 🐛 names the clip when every other encoder scored ≥0.75 MOS higher on that exact clip -- likely a defect specific to this encoder; see Quality Outliers under Issues Worth Investigating below.

#### LC Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) | fdkaac 1.0.0 | FFmpeg AAC 6.1.1-3ubuntu5 |
| :--- | :---: | :---: | :---: |
| 32k_stereo_16k | **1.746** ███░░░░░ | 1.570 ███░░░░░ | 1.144 ██░░░░░░ |
| 32k_stereo_48k | **3.949** ██████░░ | 2.521 ████░░░░ | 2.256 ████░░░░ |
| 32k_stereo_64k | **4.113** ███████░ | 4.001 ██████░░ | 3.542 ██████░░ |
| 32k_stereo_80k | 4.268 ███████░ | **4.525** ███████░ | 4.193 ███████░ |
| 32k_stereo_96k | 4.393 ███████░ | **4.623** ███████░ | 4.335 ███████░ |

#### HE-v1 Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) |
| :--- | :---: |
| 32k_stereo_16k | **1.188** ██░░░░░░ |
| 32k_stereo_48k | **1.934** ███░░░░░ 🐛 |
| 32k_stereo_64k | **2.525** ████░░░░ 🐛 |
| 32k_stereo_80k | **3.138** █████░░░ 🐛 |
| 32k_stereo_96k | **3.267** █████░░░ 🐛 |

_\* Italicized scores indicate sub-optimal profile performance superseded by another profile from the same encoder at this bitrate._

</details>

### Stereo Image Fidelity (32 kHz Stereo)

> **Note**: Measured as 1.0 - |Coherence(Ref) - Coherence(Deg)|. **Higher is truer** (closer to reference stereo image).

<details><summary><b>View Detailed Stereo Fidelity Table (32 kHz Stereo)</b></summary>

#### LC Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) | fdkaac 1.0.0 | FFmpeg AAC 6.1.1-3ubuntu5 |
| :--- | :---: | :---: | :---: |
| 32k_stereo_16k | 0.7882 ██████░░ | **0.9432** ████████ | 0.7209 ██████░░ |
| 32k_stereo_48k | 0.9543 ████████ | **0.9891** ████████ | 0.9780 ████████ |
| 32k_stereo_64k | 0.9634 ████████ | **0.9875** ████████ | 0.9842 ████████ |
| 32k_stereo_80k | 0.9762 ████████ | **0.9885** ████████ | 0.9868 ████████ |
| 32k_stereo_96k | 0.9843 ████████ | **0.9922** ████████ | 0.9904 ████████ |

#### HE-v1 Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) |
| :--- | :---: |
| 32k_stereo_16k | **0.8880** ███████░ |
| 32k_stereo_48k | **0.9702** ████████ |
| 32k_stereo_64k | **0.9852** ████████ |
| 32k_stereo_80k | **0.9902** ████████ |
| 32k_stereo_96k | **0.9918** ████████ |

</details>

### Transient Fidelity (32 kHz Stereo)

> **Note**: Measured as 1 / (1 + mean |attack-centroid-shift| ms) across onsets. **Higher is truer** (attack timing closer to reference).

<details><summary><b>View Detailed Transient Fidelity Table (32 kHz Stereo)</b></summary>

#### LC Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) | fdkaac 1.0.0 | FFmpeg AAC 6.1.1-3ubuntu5 |
| :--- | :---: | :---: | :---: |
| 32k_stereo_16k | 0.6411 █████░░░ | **0.8097** ██████░░ | 0.6680 █████░░░ |
| 32k_stereo_48k | 0.9062 ███████░ | **0.9329** ███████░ | 0.8881 ███████░ |
| 32k_stereo_64k | 0.9332 ███████░ | **0.9518** ████████ | 0.9156 ███████░ |
| 32k_stereo_80k | 0.9498 ████████ | **0.9606** ████████ | 0.9357 ███████░ |
| 32k_stereo_96k | 0.9523 ████████ | **0.9691** ████████ | 0.9445 ████████ |

#### HE-v1 Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) |
| :--- | :---: |
| 32k_stereo_16k | **0.8069** ██████░░ |
| 32k_stereo_48k | **0.9301** ███████░ |
| 32k_stereo_64k | **0.9474** ████████ |
| 32k_stereo_80k | **0.9615** ████████ |
| 32k_stereo_96k | **0.9657** ████████ |

</details>

### Bitrate Accuracy (32 kHz Stereo)

> **Note**: Deviation from target bitrate calculated from pure elementary stream audio bytes. **Lower is Better**.

<details><summary><b>View Detailed Bitrate Accuracy Table (32 kHz Stereo)</b></summary>

#### LC Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) | fdkaac 1.0.0 | FFmpeg AAC 6.1.1-3ubuntu5 |
| :--- | :---: | :---: | :---: |
| 32k_stereo_16k | **3.9%** | 6.5% | 29.9% |
| 32k_stereo_48k | 2.6% | **1.7%** | 2.5% |
| 32k_stereo_64k | **0.4%** | 1.7% | 1.3% |
| 32k_stereo_80k | **0.4%** | 1.4% | 1.6% |
| 32k_stereo_96k | **0.7%** | 1.2% | 3.3% |

#### HE-v1 Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) |
| :--- | :---: |
| 32k_stereo_16k | **1.4%** |
| 32k_stereo_48k | **0.4%** |
| 32k_stereo_64k | **0.6%** |
| 32k_stereo_80k | **0.5%** |
| 32k_stereo_96k | **0.4%** |

</details>

### 44.1 kHz Stereo Quality Across Bitrates

<details><summary><b>View Detailed 44.1 kHz Stereo Average & Worst MOS Tables</b></summary>

#### Per-Scenario Average MOS (44.1 kHz Stereo)

#### LC Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) | fdkaac 1.0.0 | FFmpeg AAC 6.1.1-3ubuntu5 |
| :--- | :---: | :---: | :---: |
| 44k1_stereo_64k | 4.223 ███████░ | **4.269** ███████░ | 3.875 ██████░░ |
| 44k1_stereo_128k | 4.656 ███████░ | **4.811** ████████ | 4.673 ███████░ |
| 44k1_stereo_160k | 4.792 ████████ | **4.871** ████████ | 4.783 ████████ |
| 44k1_stereo_192k | 4.871 ████████ | **4.927** ████████ | 4.837 ████████ |
| 44k1_stereo_256k | 4.952 ████████ | **4.960** ████████ | 4.902 ████████ |

#### HE-v1 Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) |
| :--- | :---: |
| 44k1_stereo_64k | _**3.255**_ █████░░░ * |
| 44k1_stereo_128k | _**4.493**_ ███████░ * |
| 44k1_stereo_160k | _**4.544**_ ███████░ * |
| 44k1_stereo_192k | _**4.562**_ ███████░ * |
| 44k1_stereo_256k | _**4.577**_ ███████░ * |

#### Per-Scenario Worst MOS (Min Clip MOS - 44.1 kHz Stereo)

> **Note**: Minimum perceptual MOS score observed across any clip in the scenario. Highlights edge-case clip degradation. A 🐛 names the clip when every other encoder scored ≥0.75 MOS higher on that exact clip -- likely a defect specific to this encoder; see Quality Outliers under Issues Worth Investigating below.

#### LC Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) | fdkaac 1.0.0 | FFmpeg AAC 6.1.1-3ubuntu5 |
| :--- | :---: | :---: | :---: |
| 44k1_stereo_64k | **4.019** ██████░░ | 3.814 ██████░░ | 3.333 █████░░░ |
| 44k1_stereo_128k | 4.310 ███████░ | **4.613** ███████░ | 4.416 ███████░ |
| 44k1_stereo_160k | 4.659 ███████░ | **4.693** ████████ | 4.601 ███████░ |
| 44k1_stereo_192k | 4.780 ████████ | **4.822** ████████ | 4.686 ███████░ |
| 44k1_stereo_256k | **4.916** ████████ | 4.895 ████████ | 4.773 ████████ |

#### HE-v1 Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) |
| :--- | :---: |
| 44k1_stereo_64k | **2.164** ███░░░░░ 🐛 |
| 44k1_stereo_128k | **4.192** ███████░ |
| 44k1_stereo_160k | **4.266** ███████░ |
| 44k1_stereo_192k | **4.282** ███████░ |
| 44k1_stereo_256k | **4.322** ███████░ |

_\* Italicized scores indicate sub-optimal profile performance superseded by another profile from the same encoder at this bitrate._

</details>

### Stereo Image Fidelity (44.1 kHz Stereo)

> **Note**: Measured as 1.0 - |Coherence(Ref) - Coherence(Deg)|. **Higher is truer** (closer to reference stereo image).

<details><summary><b>View Detailed Stereo Fidelity Table (44.1 kHz Stereo)</b></summary>

#### LC Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) | fdkaac 1.0.0 | FFmpeg AAC 6.1.1-3ubuntu5 |
| :--- | :---: | :---: | :---: |
| 44k1_stereo_64k | 0.9558 ████████ | **0.9841** ████████ | 0.9769 ████████ |
| 44k1_stereo_128k | 0.9811 ████████ | **0.9925** ████████ | 0.9913 ████████ |
| 44k1_stereo_160k | 0.9866 ████████ | 0.9934 ████████ | **0.9945** ████████ |
| 44k1_stereo_192k | 0.9888 ████████ | 0.9953 ████████ | **0.9957** ████████ |
| 44k1_stereo_256k | 0.9935 ████████ | 0.9956 ████████ | **0.9981** ████████ |

#### HE-v1 Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) |
| :--- | :---: |
| 44k1_stereo_64k | **0.9737** ████████ |
| 44k1_stereo_128k | **0.9932** ████████ |
| 44k1_stereo_160k | **0.9945** ████████ |
| 44k1_stereo_192k | **0.9953** ████████ |
| 44k1_stereo_256k | **0.9954** ████████ |

</details>

### Transient Fidelity (44.1 kHz Stereo)

> **Note**: Measured as 1 / (1 + mean |attack-centroid-shift| ms) across onsets. **Higher is truer** (attack timing closer to reference).

<details><summary><b>View Detailed Transient Fidelity Table (44.1 kHz Stereo)</b></summary>

#### LC Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) | fdkaac 1.0.0 | FFmpeg AAC 6.1.1-3ubuntu5 |
| :--- | :---: | :---: | :---: |
| 44k1_stereo_64k | **0.9118** ███████░ | 0.9036 ███████░ | 0.8583 ███████░ |
| 44k1_stereo_128k | **0.9560** ████████ | 0.9487 ████████ | 0.9285 ███████░ |
| 44k1_stereo_160k | **0.9620** ████████ | 0.9498 ████████ | 0.9563 ████████ |
| 44k1_stereo_192k | **0.9741** ████████ | 0.9631 ████████ | 0.9661 ████████ |
| 44k1_stereo_256k | **0.9813** ████████ | 0.9645 ████████ | 0.9810 ████████ |

#### HE-v1 Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) |
| :--- | :---: |
| 44k1_stereo_64k | **0.9192** ███████░ |
| 44k1_stereo_128k | **0.9532** ████████ |
| 44k1_stereo_160k | **0.9597** ████████ |
| 44k1_stereo_192k | **0.9627** ████████ |
| 44k1_stereo_256k | **0.9661** ████████ |

</details>

### Bitrate Accuracy (44.1 kHz Stereo)

> **Note**: Deviation from target bitrate calculated from pure elementary stream audio bytes. **Lower is Better**.

<details><summary><b>View Detailed Bitrate Accuracy Table (44.1 kHz Stereo)</b></summary>

#### LC Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) | fdkaac 1.0.0 | FFmpeg AAC 6.1.1-3ubuntu5 |
| :--- | :---: | :---: | :---: |
| 44k1_stereo_64k | 2.2% | 1.6% | **1.6%** |
| 44k1_stereo_128k | **0.5%** | 0.8% | 5.4% |
| 44k1_stereo_160k | 0.8% | **0.7%** | 6.4% |
| 44k1_stereo_192k | 0.8% | **0.6%** | 6.4% |
| 44k1_stereo_256k | 0.7% | **0.5%** | 7.8% |

#### HE-v1 Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) |
| :--- | :---: |
| 44k1_stereo_64k | **0.3%** |
| 44k1_stereo_128k | **0.6%** |
| 44k1_stereo_160k | **0.5%** |
| 44k1_stereo_192k | **0.5%** |
| 44k1_stereo_256k | **8.5%** |

</details>

### 48 kHz Stereo Quality Across Bitrates

<details><summary><b>View Detailed 48 kHz Stereo Average & Worst MOS Tables</b></summary>

#### Per-Scenario Average MOS (48 kHz Stereo)

#### LC Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) | fdkaac 1.0.0 | FFmpeg AAC 6.1.1-3ubuntu5 |
| :--- | :---: | :---: | :---: |
| 48k_stereo_24k | **2.531** ████░░░░ | 2.084 ███░░░░░ | 1.414 ██░░░░░░ |
| 48k_stereo_32k | **3.225** █████░░░ | 2.567 ████░░░░ | 1.502 ██░░░░░░ |
| 48k_stereo_40k | **3.649** ██████░░ | 2.798 ████░░░░ | 2.427 ████░░░░ |
| 48k_stereo_48k | **3.853** ██████░░ | 2.966 █████░░░ | 2.803 ████░░░░ |
| 48k_stereo_56k | 3.990 ██████░░ | **4.093** ███████░ | 3.040 █████░░░ |
| 48k_stereo_64k | 4.126 ███████░ | **4.228** ███████░ | 3.782 ██████░░ |
| 48k_stereo_96k | 4.460 ███████░ | **4.677** ███████░ | 4.394 ███████░ |
| 48k_stereo_128k | 4.655 ███████░ | **4.806** ████████ | 4.657 ███████░ |
| 48k_stereo_160k | 4.754 ████████ | **4.868** ████████ | 4.765 ████████ |
| 48k_stereo_192k | 4.854 ████████ | **4.926** ████████ | 4.814 ████████ |
| 48k_stereo_256k | 4.941 ████████ | **4.959** ████████ | 4.885 ████████ |
| 48k_stereo_320k | **4.978** ████████ | 4.969 ████████ | 4.946 ████████ |

#### HE-v1 Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) |
| :--- | :---: |
| 48k_stereo_24k | _**1.627**_ ███░░░░░ * |
| 48k_stereo_32k | _**1.808**_ ███░░░░░ * |
| 48k_stereo_40k | _**2.013**_ ███░░░░░ * |
| 48k_stereo_48k | _**2.284**_ ████░░░░ * |
| 48k_stereo_56k | _**2.718**_ ████░░░░ * |
| 48k_stereo_64k | _**3.151**_ █████░░░ * |
| 48k_stereo_96k | _**4.209**_ ███████░ * |
| 48k_stereo_128k | _**4.581**_ ███████░ * |
| 48k_stereo_160k | _**4.644**_ ███████░ * |
| 48k_stereo_192k | _**4.665**_ ███████░ * |
| 48k_stereo_256k | _**4.688**_ ████████ * |
| 48k_stereo_320k | _**4.688**_ ████████ * |

#### Per-Scenario Worst MOS (Min Clip MOS - 48 kHz Stereo)

> **Note**: Minimum perceptual MOS score observed across any clip in the scenario. Highlights edge-case clip degradation. A 🐛 names the clip when every other encoder scored ≥0.75 MOS higher on that exact clip -- likely a defect specific to this encoder; see Quality Outliers under Issues Worth Investigating below.

#### LC Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) | fdkaac 1.0.0 | FFmpeg AAC 6.1.1-3ubuntu5 |
| :--- | :---: | :---: | :---: |
| 48k_stereo_24k | 1.735 ███░░░░░ | **1.778** ███░░░░░ | 1.164 ██░░░░░░ 🐛 |
| 48k_stereo_32k | **2.633** ████░░░░ | 2.080 ███░░░░░ | 1.143 ██░░░░░░ 🐛 |
| 48k_stereo_40k | **3.392** █████░░░ | 2.102 ███░░░░░ | 1.961 ███░░░░░ |
| 48k_stereo_48k | **3.624** ██████░░ | 2.327 ████░░░░ | 2.144 ███░░░░░ |
| 48k_stereo_56k | **3.786** ██████░░ | 3.669 ██████░░ | 2.584 ████░░░░ |
| 48k_stereo_64k | **3.973** ██████░░ | 3.729 ██████░░ | 3.241 █████░░░ |
| 48k_stereo_96k | 4.290 ███████░ | **4.453** ███████░ | 3.961 ██████░░ |
| 48k_stereo_128k | 4.453 ███████░ | **4.603** ███████░ | 4.406 ███████░ |
| 48k_stereo_160k | 4.620 ███████░ | **4.688** ████████ | 4.564 ███████░ |
| 48k_stereo_192k | 4.751 ████████ | **4.818** ████████ | 4.611 ███████░ |
| 48k_stereo_256k | 4.888 ████████ | **4.890** ████████ | 4.725 ████████ |
| 48k_stereo_320k | **4.960** ████████ | 4.915 ████████ | 4.869 ████████ |

#### HE-v1 Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) |
| :--- | :---: |
| 48k_stereo_24k | **1.325** ██░░░░░░ 🐛 |
| 48k_stereo_32k | **1.644** ███░░░░░ |
| 48k_stereo_40k | **1.707** ███░░░░░ 🐛 |
| 48k_stereo_48k | **1.754** ███░░░░░ 🐛 |
| 48k_stereo_56k | **1.908** ███░░░░░ 🐛 |
| 48k_stereo_64k | **2.078** ███░░░░░ 🐛 |
| 48k_stereo_96k | **3.253** █████░░░ 🐛 |
| 48k_stereo_128k | **4.308** ███████░ |
| 48k_stereo_160k | **4.422** ███████░ |
| 48k_stereo_192k | **4.440** ███████░ |
| 48k_stereo_256k | **4.487** ███████░ |
| 48k_stereo_320k | **4.486** ███████░ |

_\* Italicized scores indicate sub-optimal profile performance superseded by another profile from the same encoder at this bitrate._

</details>

### Stereo Image Fidelity (48 kHz Stereo)

> **Note**: Measured as 1.0 - |Coherence(Ref) - Coherence(Deg)|. **Higher is truer** (closer to reference stereo image).

<details><summary><b>View Detailed Stereo Fidelity Table (48 kHz Stereo)</b></summary>

#### LC Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) | fdkaac 1.0.0 | FFmpeg AAC 6.1.1-3ubuntu5 |
| :--- | :---: | :---: | :---: |
| 48k_stereo_24k | 0.7778 ██████░░ | **0.9436** ████████ | 0.7794 ██████░░ |
| 48k_stereo_32k | 0.8756 ███████░ | **0.9577** ████████ | 0.8999 ███████░ |
| 48k_stereo_40k | 0.9410 ████████ | **0.9560** ████████ | 0.9425 ████████ |
| 48k_stereo_48k | 0.9482 ████████ | **0.9736** ████████ | 0.9617 ████████ |
| 48k_stereo_56k | 0.9520 ████████ | **0.9784** ████████ | 0.9725 ████████ |
| 48k_stereo_64k | 0.9546 ████████ | **0.9821** ████████ | 0.9751 ████████ |
| 48k_stereo_96k | 0.9705 ████████ | **0.9882** ████████ | 0.9877 ████████ |
| 48k_stereo_128k | 0.9784 ████████ | **0.9919** ████████ | 0.9914 ████████ |
| 48k_stereo_160k | 0.9848 ████████ | 0.9930 ████████ | **0.9935** ████████ |
| 48k_stereo_192k | 0.9880 ████████ | 0.9950 ████████ | **0.9954** ████████ |
| 48k_stereo_256k | 0.9925 ████████ | 0.9954 ████████ | **0.9981** ████████ |
| 48k_stereo_320k | 0.9955 ████████ | 0.9957 ████████ | **0.9989** ████████ |

#### HE-v1 Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) |
| :--- | :---: |
| 48k_stereo_24k | **0.9207** ███████░ |
| 48k_stereo_32k | **0.9329** ███████░ |
| 48k_stereo_40k | **0.9428** ████████ |
| 48k_stereo_48k | **0.9525** ████████ |
| 48k_stereo_56k | **0.9616** ████████ |
| 48k_stereo_64k | **0.9681** ████████ |
| 48k_stereo_96k | **0.9857** ████████ |
| 48k_stereo_128k | **0.9915** ████████ |
| 48k_stereo_160k | **0.9940** ████████ |
| 48k_stereo_192k | **0.9948** ████████ |
| 48k_stereo_256k | **0.9951** ████████ |
| 48k_stereo_320k | **0.9951** ████████ |

</details>

### Transient Fidelity (48 kHz Stereo)

> **Note**: Measured as 1 / (1 + mean |attack-centroid-shift| ms) across onsets. **Higher is truer** (attack timing closer to reference).

<details><summary><b>View Detailed Transient Fidelity Table (48 kHz Stereo)</b></summary>

#### LC Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) | fdkaac 1.0.0 | FFmpeg AAC 6.1.1-3ubuntu5 |
| :--- | :---: | :---: | :---: |
| 48k_stereo_24k | 0.6950 ██████░░ | **0.7847** ██████░░ | 0.6585 █████░░░ |
| 48k_stereo_32k | 0.8062 ██████░░ | **0.8268** ███████░ | 0.6967 ██████░░ |
| 48k_stereo_40k | **0.8674** ███████░ | 0.8599 ███████░ | 0.7507 ██████░░ |
| 48k_stereo_48k | 0.8677 ███████░ | **0.8807** ███████░ | 0.8084 ██████░░ |
| 48k_stereo_56k | **0.8939** ███████░ | 0.8871 ███████░ | 0.8462 ███████░ |
| 48k_stereo_64k | 0.9077 ███████░ | **0.9111** ███████░ | 0.8591 ███████░ |
| 48k_stereo_96k | **0.9449** ████████ | 0.9422 ████████ | 0.9160 ███████░ |
| 48k_stereo_128k | **0.9546** ████████ | 0.9501 ████████ | 0.9332 ███████░ |
| 48k_stereo_160k | **0.9650** ████████ | 0.9505 ████████ | 0.9546 ████████ |
| 48k_stereo_192k | **0.9658** ████████ | 0.9627 ████████ | 0.9656 ████████ |
| 48k_stereo_256k | 0.9735 ████████ | 0.9671 ████████ | **0.9776** ████████ |
| 48k_stereo_320k | 0.9798 ████████ | 0.9682 ████████ | **0.9867** ████████ |

#### HE-v1 Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) |
| :--- | :---: |
| 48k_stereo_24k | **0.8392** ███████░ |
| 48k_stereo_32k | **0.8790** ███████░ |
| 48k_stereo_40k | **0.9008** ███████░ |
| 48k_stereo_48k | **0.9104** ███████░ |
| 48k_stereo_56k | **0.9164** ███████░ |
| 48k_stereo_64k | **0.9248** ███████░ |
| 48k_stereo_96k | **0.9437** ████████ |
| 48k_stereo_128k | **0.9501** ████████ |
| 48k_stereo_160k | **0.9540** ████████ |
| 48k_stereo_192k | **0.9609** ████████ |
| 48k_stereo_256k | **0.9638** ████████ |
| 48k_stereo_320k | **0.9641** ████████ |

</details>

### Bitrate Accuracy (48 kHz Stereo)

> **Note**: Deviation from target bitrate calculated from pure elementary stream audio bytes. **Lower is Better**.

<details><summary><b>View Detailed Bitrate Accuracy Table (48 kHz Stereo)</b></summary>

#### LC Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) | fdkaac 1.0.0 | FFmpeg AAC 6.1.1-3ubuntu5 |
| :--- | :---: | :---: | :---: |
| 48k_stereo_24k | **4.7%** | 4.8% | 6.2% |
| 48k_stereo_32k | **1.5%** | 3.5% | 1.8% |
| 48k_stereo_40k | 3.7% | 2.4% | **1.4%** |
| 48k_stereo_48k | 4.3% | 1.8% | **1.0%** |
| 48k_stereo_56k | 3.6% | 1.9% | **1.4%** |
| 48k_stereo_64k | 2.7% | 1.7% | **1.4%** |
| 48k_stereo_96k | 1.1% | **1.0%** | 1.7% |
| 48k_stereo_128k | **0.6%** | 0.7% | 6.1% |
| 48k_stereo_160k | 0.8% | **0.6%** | 7.0% |
| 48k_stereo_192k | 0.8% | **0.6%** | 6.7% |
| 48k_stereo_256k | 0.8% | **0.5%** | 6.5% |
| 48k_stereo_320k | 0.8% | **0.5%** | 7.0% |

#### HE-v1 Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) |
| :--- | :---: |
| 48k_stereo_24k | **0.7%** |
| 48k_stereo_32k | **0.8%** |
| 48k_stereo_40k | **0.8%** |
| 48k_stereo_48k | **0.6%** |
| 48k_stereo_56k | **0.4%** |
| 48k_stereo_64k | **0.4%** |
| 48k_stereo_96k | **0.7%** |
| 48k_stereo_128k | **0.6%** |
| 48k_stereo_160k | **0.5%** |
| 48k_stereo_192k | **0.5%** |
| 48k_stereo_256k | **2.4%** |
| 48k_stereo_320k | **20.8%** |

</details>

### 44.1 kHz 5.1 Surround Quality Across Bitrates

<details><summary><b>View Detailed 44.1 kHz 5.1 Surround Average & Worst MOS Tables</b></summary>

#### Per-Scenario Average MOS (44.1 kHz 5.1 Surround)

#### LC Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) | fdkaac 1.0.0 | FFmpeg AAC 6.1.1-3ubuntu5 |
| :--- | :---: | :---: | :---: |
| 44k1_51_96k | 4.982 ████████ | **4.999** ████████ | 3.243 █████░░░ |
| 44k1_51_160k | 4.739 ████████ | **4.999** ████████ | 4.452 ███████░ |
| 44k1_51_224k | _4.775_ ████████ * | **4.999** ████████ | 4.938 ████████ |
| 44k1_51_256k | 4.783 ████████ | **4.999** ████████ | 4.931 ████████ |
| 44k1_51_384k | 4.955 ████████ | **4.999** ████████ | 4.994 ████████ |
| 44k1_51_448k | 4.998 ████████ | **4.999** ████████ | 4.982 ████████ |

#### HE-v1 Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) |
| :--- | :---: |
| 44k1_51_96k | _**4.176**_ ███████░ * |
| 44k1_51_160k | _**4.506**_ ███████░ * |
| 44k1_51_224k | **4.802** ████████ |
| 44k1_51_256k | _**4.632**_ ███████░ * |
| 44k1_51_384k | _**4.620**_ ███████░ * |
| 44k1_51_448k | _**4.893**_ ████████ * |

#### Per-Scenario Worst MOS (Min Clip MOS - 44.1 kHz 5.1 Surround)

> **Note**: Minimum perceptual MOS score observed across any clip in the scenario. Highlights edge-case clip degradation. A 🐛 names the clip when every other encoder scored ≥0.75 MOS higher on that exact clip -- likely a defect specific to this encoder; see Quality Outliers under Issues Worth Investigating below.

#### LC Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) | fdkaac 1.0.0 | FFmpeg AAC 6.1.1-3ubuntu5 |
| :--- | :---: | :---: | :---: |
| 44k1_51_96k | 4.982 ████████ | **4.999** ████████ | 3.243 █████░░░ 🐛 |
| 44k1_51_160k | 4.739 ████████ | **4.999** ████████ | 4.452 ███████░ |
| 44k1_51_224k | 4.775 ████████ | **4.999** ████████ | 4.938 ████████ |
| 44k1_51_256k | 4.783 ████████ | **4.999** ████████ | 4.931 ████████ |
| 44k1_51_384k | 4.955 ████████ | **4.999** ████████ | 4.994 ████████ |
| 44k1_51_448k | 4.998 ████████ | **4.999** ████████ | 4.982 ████████ |

#### HE-v1 Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) |
| :--- | :---: |
| 44k1_51_96k | **4.176** ███████░ |
| 44k1_51_160k | **4.506** ███████░ |
| 44k1_51_224k | **4.802** ████████ |
| 44k1_51_256k | **4.632** ███████░ |
| 44k1_51_384k | **4.620** ███████░ |
| 44k1_51_448k | **4.893** ████████ |

_\* Italicized scores indicate sub-optimal profile performance superseded by another profile from the same encoder at this bitrate._

</details>

### Stereo Image Fidelity (44.1 kHz 5.1 Surround)

> **Note**: Measured as 1.0 - |Coherence(Ref) - Coherence(Deg)|. **Higher is truer** (closer to reference stereo image).

<details><summary><b>View Detailed Stereo Fidelity Table (44.1 kHz 5.1 Surround)</b></summary>

#### LC Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) | fdkaac 1.0.0 | FFmpeg AAC 6.1.1-3ubuntu5 |
| :--- | :---: | :---: | :---: |
| 44k1_51_96k | 0.9983 ████████ | **1.0000** ████████ | 0.9939 ████████ |
| 44k1_51_160k | **0.9984** ████████ | 0.9982 ████████ | 0.9979 ████████ |
| 44k1_51_224k | 0.9991 ████████ | **1.0000** ████████ | 0.9989 ████████ |
| 44k1_51_256k | 0.9984 ████████ | **1.0000** ████████ | 0.9978 ████████ |
| 44k1_51_384k | **0.9995** ████████ | 0.9982 ████████ | 0.9991 ████████ |
| 44k1_51_448k | 0.9980 ████████ | 0.9982 ████████ | **0.9999** ████████ |

#### HE-v1 Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) |
| :--- | :---: |
| 44k1_51_96k | **0.9810** ████████ |
| 44k1_51_160k | **0.9813** ████████ |
| 44k1_51_224k | **0.9825** ████████ |
| 44k1_51_256k | **0.9826** ████████ |
| 44k1_51_384k | **0.9954** ████████ |
| 44k1_51_448k | **0.9976** ████████ |

</details>

### Transient Fidelity (44.1 kHz 5.1 Surround)

> **Note**: Measured as 1 / (1 + mean |attack-centroid-shift| ms) across onsets. **Higher is truer** (attack timing closer to reference).

<details><summary><b>View Detailed Transient Fidelity Table (44.1 kHz 5.1 Surround)</b></summary>

#### LC Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) | fdkaac 1.0.0 | FFmpeg AAC 6.1.1-3ubuntu5 |
| :--- | :---: | :---: | :---: |
| 44k1_51_96k | 0.9601 ████████ | **0.9949** ████████ | 0.9035 ███████░ |
| 44k1_51_160k | 0.9880 ████████ | **0.9937** ████████ | 0.9405 ████████ |
| 44k1_51_224k | 0.9696 ████████ | **0.9951** ████████ | 0.9818 ████████ |
| 44k1_51_256k | 0.9829 ████████ | **0.9951** ████████ | 0.9949 ████████ |
| 44k1_51_384k | 0.9630 ████████ | 0.9951 ████████ | **0.9969** ████████ |
| 44k1_51_448k | 0.9821 ████████ | **0.9951** ████████ | 0.9771 ████████ |

#### HE-v1 Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) |
| :--- | :---: |
| 44k1_51_96k | **0.9991** ████████ |
| 44k1_51_160k | **0.9814** ████████ |
| 44k1_51_224k | **0.9911** ████████ |
| 44k1_51_256k | **0.9941** ████████ |
| 44k1_51_384k | **0.9966** ████████ |
| 44k1_51_448k | **0.9929** ████████ |

</details>

### Bitrate Accuracy (44.1 kHz 5.1 Surround)

> **Note**: Deviation from target bitrate calculated from pure elementary stream audio bytes. **Lower is Better**.

<details><summary><b>View Detailed Bitrate Accuracy Table (44.1 kHz 5.1 Surround)</b></summary>

#### LC Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) | fdkaac 1.0.0 | FFmpeg AAC 6.1.1-3ubuntu5 |
| :--- | :---: | :---: | :---: |
| 44k1_51_96k | 0.4% | 0.7% | **0.3%** |
| 44k1_51_160k | **0.2%** | 0.6% | 0.2% |
| 44k1_51_224k | 0.4% | 0.6% | **0.2%** |
| 44k1_51_256k | 0.2% | 0.5% | **0.1%** |
| 44k1_51_384k | 0.7% | 0.5% | **0.2%** |
| 44k1_51_448k | 0.9% | 0.5% | **0.3%** |

#### HE-v1 Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) |
| :--- | :---: |
| 44k1_51_96k | **1.7%** |
| 44k1_51_160k | **0.0%** |
| 44k1_51_224k | **0.7%** |
| 44k1_51_256k | **0.7%** |
| 44k1_51_384k | **0.7%** |
| 44k1_51_448k | **0.7%** |

</details>

### BD-Rate Relative Efficiency (vs FAAC 2.1.0 (1c127b8-dirty))

> **Note**: Bjontegaard-delta rate (BD-rate) measures the average percentage difference in bitrate for equal perceptual quality (MOS). **Negative % = candidate is more efficient** (uses fewer bits for same quality). BD-rate holds quality fixed by construction, avoiding bitrate-bias traps of raw fixed-rate MOS deltas.

| Encoder | Profile | BD-Rate % vs Baseline |
| :--- | :---: | :---: |
| fdkaac 1.0.0 | LC | +28.96% |
| FFmpeg AAC 6.1.1-3ubuntu5 | LC | +17.24% |

### Encoder Efficiency & Footprint

<details><summary><b>View Detailed Per-Scenario Efficiency Table</b></summary>

#### LC Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) | fdkaac 1.0.0 | FFmpeg AAC 6.1.1-3ubuntu5 |
| :--- | :---: | :---: | :---: |
| 16k_mono_20k | **116.4x** ██████░░ | 102.7x █████░░░ | 28.9x ██░░░░░░ |
| 16k_mono_24k | **147.6x** ████████ | 104.2x █████░░░ | 25.6x █░░░░░░░ |
| 16k_mono_voip_24k | **98.7x** █████░░░ | 85.5x ████░░░░ | 29.4x ██░░░░░░ |
| 24k_mono_28k | **118.5x** ██████░░ | 102.8x █████░░░ | 35.6x ██░░░░░░ |
| 24k_mono_32k | **148.2x** ████████ | 98.8x █████░░░ | 35.0x ██░░░░░░ |
| 32k_stereo_16k | **118.0x** ██████░░ | 83.8x ████░░░░ | 18.4x █░░░░░░░ |
| 32k_stereo_48k | **150.8x** ████████ | 60.7x ███░░░░░ | 18.3x █░░░░░░░ |
| 32k_stereo_64k | **150.3x** ████████ | 52.1x ███░░░░░ | 15.3x █░░░░░░░ |
| 32k_stereo_80k | **132.3x** ███████░ | 46.0x ██░░░░░░ | 15.0x █░░░░░░░ |
| 32k_stereo_96k | **147.5x** ████████ | 47.3x ██░░░░░░ | 14.9x █░░░░░░░ |
| 44k1_stereo_64k | **115.4x** ██████░░ | 48.3x ███░░░░░ | 15.2x █░░░░░░░ |
| 44k1_stereo_128k | **120.6x** ██████░░ | 39.6x ██░░░░░░ | 12.2x █░░░░░░░ |
| 44k1_stereo_160k | **121.5x** ██████░░ | 41.2x ██░░░░░░ | 12.7x █░░░░░░░ |
| 44k1_stereo_192k | **129.7x** ███████░ | 40.2x ██░░░░░░ | 12.6x █░░░░░░░ |
| 44k1_stereo_256k | **125.7x** ███████░ | 37.7x ██░░░░░░ | 14.4x █░░░░░░░ |
| 48k_stereo_24k | **122.3x** ██████░░ | 67.1x ████░░░░ | 18.6x █░░░░░░░ |
| 48k_stereo_32k | **152.2x** ████████ | 62.9x ███░░░░░ | 16.4x █░░░░░░░ |
| 48k_stereo_40k | **138.0x** ███████░ | 52.0x ███░░░░░ | 14.9x █░░░░░░░ |
| 48k_stereo_48k | **146.3x** ████████ | 54.6x ███░░░░░ | 14.8x █░░░░░░░ |
| 48k_stereo_56k | **146.1x** ████████ | 47.9x ███░░░░░ | 15.7x █░░░░░░░ |
| 48k_stereo_64k | **145.7x** ████████ | 47.2x ██░░░░░░ | 15.1x █░░░░░░░ |
| 48k_stereo_96k | **140.2x** ███████░ | 44.1x ██░░░░░░ | 13.9x █░░░░░░░ |
| 48k_stereo_128k | **134.1x** ███████░ | 41.8x ██░░░░░░ | 14.1x █░░░░░░░ |
| 48k_stereo_160k | **131.4x** ███████░ | 39.9x ██░░░░░░ | 13.4x █░░░░░░░ |
| 48k_stereo_192k | **128.3x** ███████░ | 39.3x ██░░░░░░ | 12.8x █░░░░░░░ |
| 48k_stereo_256k | **124.0x** ███████░ | 36.7x ██░░░░░░ | 13.8x █░░░░░░░ |
| 48k_stereo_320k | **102.6x** █████░░░ | 32.4x ██░░░░░░ | 12.0x █░░░░░░░ |
| 44k1_51_96k | **50.3x** ███░░░░░ | 44.3x ██░░░░░░ | 10.4x █░░░░░░░ |
| 44k1_51_160k | **100.3x** █████░░░ | 34.0x ██░░░░░░ | 8.0x ░░░░░░░░ |
| 44k1_51_224k | **98.2x** █████░░░ | 32.4x ██░░░░░░ | 8.2x ░░░░░░░░ |
| 44k1_51_256k | **94.8x** █████░░░ | 35.5x ██░░░░░░ | 8.4x ░░░░░░░░ |
| 44k1_51_384k | **86.8x** █████░░░ | 34.1x ██░░░░░░ | 7.1x ░░░░░░░░ |
| 44k1_51_448k | **82.3x** ████░░░░ | 34.9x ██░░░░░░ | 10.6x █░░░░░░░ |

#### HE-v1 Profile

| Scenario | FAAC 2.1.0 (1c127b8-dirty) |
| :--- | :---: |
| 16k_mono_20k | N/A |
| 16k_mono_24k | N/A |
| 16k_mono_voip_24k | N/A |
| 24k_mono_28k | N/A |
| 24k_mono_32k | N/A |
| 32k_stereo_16k | **122.9x** ███████░ |
| 32k_stereo_48k | **135.9x** ████████ |
| 32k_stereo_64k | **138.8x** ████████ |
| 32k_stereo_80k | **125.0x** ███████░ |
| 32k_stereo_96k | **136.7x** ████████ |
| 44k1_stereo_64k | **129.7x** ███████░ |
| 44k1_stereo_128k | **113.9x** ███████░ |
| 44k1_stereo_160k | **122.2x** ███████░ |
| 44k1_stereo_192k | **122.6x** ███████░ |
| 44k1_stereo_256k | **112.3x** ██████░░ |
| 48k_stereo_24k | **133.8x** ████████ |
| 48k_stereo_32k | **133.5x** ████████ |
| 48k_stereo_40k | **119.9x** ███████░ |
| 48k_stereo_48k | **130.7x** ████████ |
| 48k_stereo_56k | **128.6x** ███████░ |
| 48k_stereo_64k | **127.2x** ███████░ |
| 48k_stereo_96k | **125.4x** ███████░ |
| 48k_stereo_128k | **121.9x** ███████░ |
| 48k_stereo_160k | **123.5x** ███████░ |
| 48k_stereo_192k | **119.0x** ███████░ |
| 48k_stereo_256k | **111.8x** ██████░░ |
| 48k_stereo_320k | **106.8x** ██████░░ |
| 44k1_51_96k | **73.4x** ████░░░░ |
| 44k1_51_160k | **84.4x** █████░░░ |
| 44k1_51_224k | **84.8x** █████░░░ |
| 44k1_51_256k | **85.8x** █████░░░ |
| 44k1_51_384k | **80.6x** █████░░░ |
| 44k1_51_448k | **76.7x** ████░░░░ |

</details>

</details>

<details><summary><b>🐛 View Quality Outliers (Issues Worth Investigating)</b></summary>

### Quality Outliers (Issues Worth Investigating)

> **Note**: Flags clips where this encoder scored **≥0.75 MOS lower** than the average of all other encoders on the exact same clip. These clips represent isolated codec defects, killer clips, or tuning bugs.

| Encoder | Profile | Scenario | Outlier Clip | Encoder MOS | Peer Avg MOS | Defect Gap |
| :--- | :---: | :--- | :--- | :---: | :---: | :---: |
| FAAC 2.1.0 (1c127b8-dirty) | HE-v1 | 48k_stereo_64k | `velvet.16b48k.wav` | 2.08 | 3.65 | **-1.57 MOS** |
| FAAC 2.1.0 (1c127b8-dirty) | HE-v1 | 44k1_stereo_64k | `velvet.16b48k.wav` | 2.16 | 3.72 | **-1.56 MOS** |
| FFmpeg AAC 6.1.1-3ubuntu5 | LC | 44k1_51_96k | `6_Channel_ID.wav` | 3.24 | 4.72 | **-1.48 MOS** |
| FAAC 2.1.0 (1c127b8-dirty) | HE-v1 | 48k_stereo_56k | `velvet.16b48k.wav` | 1.91 | 3.37 | **-1.47 MOS** |
| FAAC 2.1.0 (1c127b8-dirty) | HE-v1 | 32k_stereo_64k | `velvet.16b48k.wav` | 2.53 | 3.89 | **-1.36 MOS** |
| FFmpeg AAC 6.1.1-3ubuntu5 | LC | 48k_stereo_32k | `velvet.16b48k.wav` | 1.14 | 2.38 | **-1.24 MOS** |
| FAAC 2.1.0 (1c127b8-dirty) | HE-v1 | 32k_stereo_80k | `velvet.16b48k.wav` | 3.14 | 4.36 | **-1.22 MOS** |
| FAAC 2.1.0 (1c127b8-dirty) | HE-v1 | 32k_stereo_96k | `velvet.16b48k.wav` | 3.27 | 4.46 | **-1.19 MOS** |
| FAAC 2.1.0 (1c127b8-dirty) | HE-v1 | 48k_stereo_48k | `velvet.16b48k.wav` | 1.75 | 2.76 | **-1.01 MOS** |
| FAAC 2.1.0 (1c127b8-dirty) | HE-v1 | 48k_stereo_96k | `velvet.16b48k.wav` | 3.25 | 4.23 | **-0.98 MOS** |
| FAAC 2.1.0 (1c127b8-dirty) | HE-v1 | 32k_stereo_48k | `velvet.16b48k.wav` | 1.93 | 2.91 | **-0.97 MOS** |
| FFmpeg AAC 6.1.1-3ubuntu5 | LC | 48k_stereo_24k | `fms.wav` | 1.16 | 2.05 | **-0.88 MOS** |
| FAAC 2.1.0 (1c127b8-dirty) | HE-v1 | 48k_stereo_40k | `velvet.16b48k.wav` | 1.71 | 2.57 | **-0.87 MOS** |
| FAAC 2.1.0 (1c127b8-dirty) | HE-v1 | 48k_stereo_24k | `sandman.16b48k.wav` | 1.33 | 2.11 | **-0.78 MOS** |

</details>


---
**Metric Legend**:
- **Ranking**: by Worst MOS, then Overall MOS as tiebreaker.
- **Quality (MOS)**: Perceptual audio quality (1-5, **Higher is Better**)
- **Stereo Fidelity**: Faithfulness of stereo image (0-1, **Higher is Better**)
- **Transient Fidelity**: How little attacks are smeared/delayed (0-1, **Higher is Better**)
- **Speed**: Encoding throughput (**Higher is Better**)
- **Bitrate Error**: Deviation from target bitrate (**Lower is Better**)
- **ROM (Flash)**: Codec code + read-only data size (**Lower is Better**)
