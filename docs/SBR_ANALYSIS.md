# Pseudo-SBR Bitrate Optimization Analysis

## Objective
To determine the optimal algorithm and tuning for Pseudo-SBR based on empirical MOS data across speech and music scenarios.

## Methodology
The encoder was evaluated using the `faac-benchmark` suite with varying core bandwidths, patching strategies, and SBR parameters.

## Algorithm Overview
Pseudo-SBR is an encoder-side "blind" bandwidth extension for AAC-LC. It performs spectral patching by translating a source region of the coded spectrum into the higher frequency bins.

### Core Features
- **Spectral Translation:** Moves the top 50% of the coded spectrum into the extension region.
- **Gain Rolloff:** Applies ≈ -9dB attenuation per successive patch (0.354f) to simulate natural spectral decay.
- **Noise Injection:** Mixes 12% white noise and a 0.005f constant comfort noise floor into the patches to prevent "metallic" artifacts.
- **Cross-fading:** Uses a 4-bin linear transition at the crossover point to minimize discontinuities.

## Bitrate-Adaptive Configuration
To prevent bit starvation and optimize quality, SBR extension is capped based on the bitrate per channel:

| Bitrate (per ch) | Expansion Limit |
| :--- | :--- |
| < 12 kbps | 15% expansion |
| < 24 kbps | 25% expansion |
| < 48 kbps | 40% expansion |

## Results Summary (Avg MOS)

| Scenario | Mode | Base MOS | Final MOS | Lift |
| :--- | :--- | :--- | :--- | :--- |
| voip (16k) | Speech | 3.000 | 3.256 | +0.256 |
| music_low (64k) | Audio | 3.292 | 3.551 | +0.259 |
| vss (40k) | Speech | 4.150 | 4.157 | +0.007 |

### Analysis
- **VoIP:** The combination of a restricted 5kHz core and 25% SBR expansion significantly improves intelligibility.
- **Music Low:** The conservative 40% activation threshold and -9dB rolloff prevent the "metallic" shimmer often associated with aggressive patching.
- **VSS:** Gains are limited by the 16kHz sampling rate, as the core coder is already highly efficient at 40kbps.
