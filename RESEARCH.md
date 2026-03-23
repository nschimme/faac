# FAAC Bandwidth vs. MOS Research

## Final Systematic Sweep Results (30% Coverage)
*Bitrate is bits-per-channel (br/ch).*

### music_low (32kbps/ch)
| Bandwidth (Hz) | Avg MOS |
|----------------|---------|
| 6000           | 2.7178  |
| 8000 (Legacy)  | 2.7178  |
| 11000          | 2.7990  |
| 14000          | 2.6943  |

### music_std (64kbps/ch)
| Bandwidth (Hz) | Avg MOS |
|----------------|---------|
| 12000          | 4.0613  |
| 17000          | 4.2217  |
| 18000 (Legacy) | 4.1899  |
| 19000          | 4.2217  |

## Final Verified Performance
Comparison between baseline (legacy) and optimized curve (5-segment model).

| Scenario | Baseline MOS | Final MOS | Delta |
|----------|--------------|-----------|-------|
| music_low| 2.7178       | 2.9723    | +0.25 |
| music_med| 2.8427       | 3.4163    | +0.57 |
| music_std| 4.1602       | 4.2002    | +0.04 |

## Final Optimized Curve
Implemented in `libfaac/util.c`:
- 0-16kbps: 4k-6k
- 16k-32kbps: 6k-11k
- 32k-64kbps: 11k-18.5k
- 64k-128kbps: 18.5k-20k
- 128kbps+: 20k
