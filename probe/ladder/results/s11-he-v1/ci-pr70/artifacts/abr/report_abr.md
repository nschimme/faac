## 🚀 Perceptual & Efficiency Improvement

### Environment
- **Baseline SHA**: `d7a6effe22e0dd86d30c9147649542b937021c7d`
- **Candidate SHA**: `c50b66aab79f754fc17e2550159a861d26403b65`

### Executive 3-Pillar Balance
| Pillar | Metric | Change vs Baseline | Status |
| :--- | :--- | :---: | :---: |
| 🎧 **Quality** | Perceptual MOS Δ | `+0.001 MOS` | ✅ Pass |
| ⚡ **Performance** | Speed (Throughput Δ) | `-0.0%` | ✅ Pass |
| 📦 **Footprint** | Code Footprint Δ | `-0.06%` | ✅ Pass |

### Summary Details
| Metric | Value |
| :--- | :--- |
| **Regressions** | 4 ⚠️ |
| **Worst MOS Drop** | -0.10 (48k_stereo_24k: clapton_44k.16b48k.wav) |
| **Max Bitrate Err** | -34.8% (44k1_51_448k: 6_Channel_ID.wav) 📉 |
| **Significant Wins** | 1 🌟 |
| **Consistency** | 67.1% |
| **Throughput Δ** | -0.0%  (LC: -0.0%, HE: -0.0%) |
| **Worst-case TP Δ** | -4.3% (amd64_abr / 16k_mono_20k) ⚠️ |
| **Library Size** | -0.06%  |
| **Bitrate Accuracy** | 98.3% 🎯 |
| **Avg MOS Δ** | +0.001 ⚠️ Quality Churn (143 clips improved, 69 degraded) |
| **Worst Stereo Drop** | -0.0070 (48k_stereo_16k: Last_Of_The_Mohicanz__Sample_.16b48k.wav) |

#### MOS Δ by Rate Family
```mermaid
xychart-beta
    title "MOS Δ by Rate Family"
    x-axis ["16 kHz Mono Speech", "24 kHz Mono Speech", "32 kHz Stereo", "44.1 kHz Stereo", "48 kHz Stereo", "44.1 kHz 5.1 Surround"]
    y-axis "MOS Δ" -0.05 --> 0.05
    bar "MOS Δ" [0.000, 0.000, 0.002, 0.001, 0.001, 0.000]
```

| Family | MOS Δ | 95% Conf. Interval | Clips |
| :--- | :---: | :---: | :---: |
| 16 kHz Mono Speech | +0.000 | [+0.000, +0.000] | 128 |
| 24 kHz Mono Speech | +0.000 | [+0.000, +0.000] | 80 |
| 32 kHz Stereo | +0.002 | [+0.000, +0.004] ✳ | 245 |
| 44.1 kHz Stereo | +0.001 | [+0.000, +0.001] ✳ | 245 |
| 48 kHz Stereo | +0.001 | [+0.001, +0.002] ✳ | 637 |
| 44.1 kHz 5.1 Surround | +0.000 | [+0.000, +0.000] | 6 |

decoder output: skipped (no decoder_diff block)

<details><summary><b>📋 View Scenario Performance Details</b></summary>

### Scenario Performance
| Scenario | Rate | MOS Δ | 95% Conf. Interval | Wins / Losses | Stereo Fid. Δ | Transient | Throughput Δ | Bitrate Acc |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **16 kHz Mono Speech** | | | | | | | | |
| 16k_mono_20k | 16k/1ch | +0.000 | [+0.000, +0.000] | 0/0 | N/A | ➖ (n=0) | -4.3% | 93.1% |
| 16k_mono_24k | 16k/1ch | +0.000 | [+0.000, +0.000] | 0/0 | N/A | ➖ (n=0) | -0.5% | 95.8% |
| 16k_mono_voip_24k | 16k/1ch | +0.000 | [+0.000, +0.000] | 0/0 | N/A | ➖ (n=0) | -0.8% | 96.5% |
| **24 kHz Mono Speech** | | | | | | | | |
| 24k_mono_28k | 24k/1ch | +0.000 | [+0.000, +0.000] | 0/0 | N/A | ➖ (n=0) | +5.4% | 96.7% |
| 24k_mono_32k | 24k/1ch | +0.000 | [+0.000, +0.000] | 0/0 | N/A | ➖ (n=0) | +1.0% | 92.6% |
| **32 kHz Stereo** | | | | | | | | |
| 32k_stereo_16k | 32k/2ch | +0.012 | [+0.003, +0.022] ✳ | 20/9 | +0.0003 | ➖ (n=1329) | -0.2% | 95.9% |
| 32k_stereo_48k | 32k/2ch | +0.000 | [+0.000, +0.000] | 0/0 | +0.0000 | ➖ (n=701) | +2.0% | 99.3% |
| 32k_stereo_64k | 32k/2ch | +0.000 | [+0.000, +0.000] | 0/0 | +0.0001 | ➖ (n=701) | -0.8% | 99.3% |
| 32k_stereo_80k | 32k/2ch | +0.000 | [+0.000, +0.000] | 0/0 | +0.0001 | ➖ (n=701) | -0.5% | 99.3% |
| 32k_stereo_96k | 32k/2ch | +0.000 | [+0.000, +0.000] | 0/0 | +0.0001 | ➖ (n=701) | +0.3% | 99.4% |
| **44.1 kHz Stereo** | | | | | | | | |
| 44k1_stereo_64k | 44.1k/2ch | +0.004 | [+0.002, +0.005] ✳ | 15/3 | +0.0001 | ➖ (n=1277) | +0.3% | 99.4% |
| 44k1_stereo_128k | 44.1k/2ch | +0.000 | [+0.000, +0.000] | 0/0 | +0.0000 | ➖ (n=701) | -0.3% | 99.3% |
| 44k1_stereo_160k | 44.1k/2ch | +0.000 | [+0.000, +0.000] | 0/0 | +0.0000 | ➖ (n=701) | +0.3% | 99.3% |
| 44k1_stereo_192k | 44.1k/2ch | +0.000 | [+0.000, +0.000] | 0/0 | +0.0000 | ➖ (n=701) | +0.1% | 99.2% |
| 44k1_stereo_256k | 44.1k/2ch | +0.000 | [+0.000, +0.000] | 0/0 | +0.0000 | ➖ (n=701) | +0.3% | 99.2% |
| **48 kHz Stereo** | | | | | | | | |
| 48k_stereo_16k | 48k/2ch | +0.002 | [-0.004, +0.007] | 14/17 | +0.0003 | ➖ (n=1321) | -0.2% | 96.4% |
| 48k_stereo_24k | 48k/2ch | -0.001 | [-0.007, +0.005] | 15/12 | +0.0000 | ➖ (n=1337) | -0.9% | 99.1% |
| 48k_stereo_32k | 48k/2ch | +0.003 | [-0.000, +0.006] | 20/10 | -0.0000 | ➖ (n=1362) | +2.5% | 99.1% |
| 48k_stereo_40k | 48k/2ch | +0.003 | [+0.000, +0.006] ✳ | 14/6 | +0.0001 | ➖ (n=1340) | -3.3% | 99.0% |
| 48k_stereo_48k | 48k/2ch | +0.003 | [+0.001, +0.006] ✳ | 15/4 | +0.0001 | ➖ (n=1300) | +0.2% | 99.1% |
| 48k_stereo_56k | 48k/2ch | +0.003 | [-0.000, +0.006] | 16/5 | +0.0001 | ➖ (n=1343) | -0.5% | 99.3% |
| 48k_stereo_64k | 48k/2ch | +0.004 | [+0.002, +0.007] ✳ | 14/3 | +0.0001 | ➖ (n=1344) | -0.3% | 99.4% |
| 48k_stereo_96k | 48k/2ch | +0.000 | [+0.000, +0.000] | 0/0 | +0.0000 | ➖ (n=701) | -0.2% | 99.3% |
| 48k_stereo_128k | 48k/2ch | +0.000 | [+0.000, +0.000] | 0/0 | +0.0000 | ➖ (n=701) | -0.4% | 99.2% |
| 48k_stereo_160k | 48k/2ch | +0.000 | [+0.000, +0.000] | 0/0 | -0.0000 | ➖ (n=701) | -0.5% | 99.2% |
| 48k_stereo_192k | 48k/2ch | +0.000 | [+0.000, +0.000] | 0/0 | -0.0000 | ➖ (n=701) | -0.3% | 99.3% |
| 48k_stereo_256k | 48k/2ch | +0.000 | [+0.000, +0.000] | 0/0 | -0.0000 | ➖ (n=701) | -0.2% | 99.2% |
| 48k_stereo_320k | 48k/2ch | +0.000 | [+0.000, +0.000] | 0/0 | -0.0000 | ➖ (n=701) | +0.9% | 98.9% |
| **44.1 kHz 5.1 Surround** | | | | | | | | |
| 44k1_51_96k | 44.1k/6ch | +0.000 | N/A | N/A | +0.0000 | ➖ (n=1) | -1.1% | 99.8% |
| 44k1_51_160k | 44.1k/6ch | +0.000 | N/A | N/A | +0.0000 | ➖ (n=1) | +0.7% | 99.9% |
| 44k1_51_224k | 44.1k/6ch | +0.000 | N/A | N/A | +0.0000 | ➖ (n=1) | +0.7% | 99.5% |
| 44k1_51_256k | 44.1k/6ch | +0.000 | N/A | N/A | +0.0000 | ➖ (n=1) | +0.3% | 99.4% |
| 44k1_51_384k | 44.1k/6ch | +0.000 | N/A | N/A | +0.0000 | ➖ (n=1) | -0.7% | 75.9% |
| 44k1_51_448k | 44.1k/6ch | +0.000 | N/A | N/A | +0.0000 | ➖ (n=1) | -0.5% | 65.2% |

_Transient fidelity measures attack centroid shift (smearing/delay of transient attacks, in ms). 📈 = improved, 📉 = regression, ➖ = neutral/insufficient onsets (<30)._
_✳ Statistically significant change (95% confidence interval excludes 0)_

</details>

<details><summary><b>❌ View Regression Details (4)</b></summary>


#### amd64_abr
| Test Case | Status | MOS (Base) | Delta | Target | Actual | Acc % | Speed Δ | Bit-Exact |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| 48k_stereo_24k: clapton_44k.16b48k.wav | ⚠️ | 3.44 (3.53) | -0.10 | 24k | 23.9k | 99.6% | -1.2% | ❌ |
| 32k_stereo_16k: clapton_44k.16b48k.wav | ⚠️ | 2.89 (2.95) | -0.07 | 16k | 16.7k | 95.5% | +1.5% | ❌ |
| 48k_stereo_16k: Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav | ⚠️ | 2.49 (2.56) | -0.07 | 16k | 16.3k | 98.1% | -0.5% | ❌ |
| 48k_stereo_24k: bas.wav | ⚠️ | 2.72 (2.77) | -0.05 | 24k | 23.8k | 99.1% | -2.6% | ❌ |

</details>

<details><summary><b>View Additional Suite Details</b></summary>


#### ✅ amd64_abr
- MOS Δ: +0.001, TP Δ: -0.0%, Size Δ: -0.06%
- Bitstream Consistency: 67.1%

**Gates**
- ⚠️ `mos`: 4 clip(s) past -0.05
- ✅ `footprint`: .text+.rodata+.data 76040 -> 75992 (-48 bytes, -0.06%)
- ✅ `throughput`: instruction count x1.000 (95% CI 1.000-1.000) over 12 signal(s)
- ✅ `bd_rate`: audio_32k/Low Complexity +0.000%, audio_44k1/Low Complexity +0.000%, audio_48k/HE-AAC v1 -0.108%, audio_48k/Low Complexity +0.000%, audio_51/Low Complexity +0.000%
- ✅ `haze`: atk_he_q50 pre-onset error +0.0 dB mean, +0.0 dB worst kick; ctl_he_q50 haze +0.0->+0.0 dB; ctl_lc_q200 haze -0.0->-0.0 dB; he_b32 haze +7.3->+7.3 dB; he_q50 haze +13.8->+13.8 dB; lc_q200 haze +15.5->+15.5 dB

**BD-rate** (positive = more bits for equal quality = worse)
| Ladder | Rungs | Clips | Mean | Median |
| :--- | :---: | :---: | :---: | :---: |
| audio_32k / Low Complexity | 4 | 49 | +0.000% | +0.000% |
| audio_44k1 / Low Complexity | 4 | 49 | +0.000% | +0.000% |
| audio_48k / HE-AAC v1 | 7 | 49 | -0.108% | -0.129% |
| audio_48k / Low Complexity | 6 | 49 | +0.000% | +0.000% |
| audio_51 / Low Complexity | 4 | 1 | +0.000% | +0.000% |
- audio_32k / HE-AAC v1: 1 rung(s), needs 3 -- skipped
- audio_44k1 / HE-AAC v1: 1 rung(s), needs 3 -- skipped
- audio_51 / HE-AAC v1: 2 rung(s), needs 3 -- skipped
- speech_clean_16k / Low Complexity: 2 rung(s), needs 3 -- skipped
- speech_clean_24k / Low Complexity: 2 rung(s), needs 3 -- skipped
- speech_voip_16k / Low Complexity: 1 rung(s), needs 3 -- skipped

**Object .text movers**
```mermaid
xychart-beta
    title "Object File .text Size Movers (Bytes)"
    x-axis ["libfaac.a.p/sbr_analysis.c.o"]
    y-axis "Byte Change" -147 --> 147
    bar "Byte Change" [-117]
```

`libfaac.a.p/sbr_analysis.c.o` -117

</details>

_Full per-clip test cases (1341 total) exported to `cases_abr.md`._

