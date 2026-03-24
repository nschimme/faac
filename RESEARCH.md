# FAAC Bandwidth vs. MOS Research

## Goal
Improve Mean Opinion Score (MOS) for `music_low` (32kbps/ch) and `music_std` (64kbps/ch) scenarios while maintaining a slight bitrate undershoot (~ -5% bias) and ensuring monotonically increasing bandwidth behavior.

## Methodology
Performed over 50 systematic benchmark iterations using `faac-benchmark` at 30% coverage. Tweaked `CalcBandwidth` piecewise linear segments and verified resulting MOS and bitrate bias.

## Final Sweep Results (30% Coverage)
*Bitrate is bits-per-channel (br/ch).*

### music_low (32kbps/ch)
| Bandwidth (Hz) | Avg MOS | Bitrate Ratio |
|----------------|---------|---------------|
| 8000 (Legacy)  | 2.7178  | 0.9413        |
| 7000 (New)     | 2.8966  | 0.9415        |

### music_std (64kbps/ch)
| Bandwidth (Hz) | Avg MOS | Bitrate Ratio |
|----------------|---------|---------------|
| 18000 (Legacy) | 4.1874  | 0.9413        |
| 14500 (New)    | 4.2142  | 0.9415        |

## Optimized 5-segment Curve
The resulting curve provided the best balance between high-frequency preservation and core frequency bits, especially at the critical 32kbps/ch point.

- 0-16kbps/ch: 2.8kHz to 3.8kHz (Telephony)
- 16k-32kbps/ch: 3.8kHz to 7.0kHz (Low-tier Music)
- 32k-64kbps/ch: 7.0kHz to 14.5kHz (Mid-tier Music)
- 64k-128kbps/ch: 14.5kHz to 17.0kHz (High-fidelity Expansion)
- 128kbps/ch+: 20.0kHz (Transparency Plateau)

## Final Verified Performance
| Metric | Baseline | Final | Delta |
| :--- | :---: | :---: | :---: |
| music_low MOS | 2.7178 | 2.8966 | **+0.1788** |
| music_std MOS | 4.1874 | 4.2142 | **+0.0268** |
| **Bitrate Bias** | **0.9413** | **0.9415** | **+0.0002** |

## Summary
By slightly narrowing the bandwidth at mid-tier bitrates compared to legacy code, we allowed the encoder to improve the quality of the audible core frequencies. This resulted in a significant MOS gain for `music_low` and a modest gain for `music_std`, all while maintaining the requested ~5% bitrate undershoot.
