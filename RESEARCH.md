
## Why did full Avg MOS Delta drop?
While the subset showed a gain of +0.342, the full 849-sample run settled at +0.147. This is primarily due to specific regressions in the `voip` scenario where very low bitrate samples (16kbps) suffered from the more aggressive bandwidth settings or reduced masking.

### Identified Regressions (Top 10):
- voip_C_14_ECHO_FG.wav: -1.74
- voip_C_14_CLIP_FG.wav: -1.53
- voip_C_14_COMPSPKR_FG.wav: -1.41
- voip_C_11_CLIP_FG.wav: -1.41
- voip_C_01_NOISE_FG.wav: -1.22
- voip_C_24_ECHO_FA.wav: -1.18
- voip_C_24_NOISE_MK.wav: -1.14
- voip_C_12_COMPSPKR_FA.wav: -1.13
- voip_C_24_ECHO_FG.wav: -1.11
- voip_C_14_COMPSPKR_FA.wav: -1.09

### Recommended Improved Subset for Future Work:
Include the above regressions plus the original subset:
`trumpet.16b48k.wav,glk.wav,cst.wav,12-German-male-speech.441.16b48k.wav,Hotel California.16b48k.wav,bonhemian_rhapsody.16b48k.wav,C_01_CHOP_FA.wav,C_05_NOISE_ML.wav,C_10_ECHO_FG.wav,C_15_CLIP_MK.wav,C_20_COMPSPKR_FA.wav,C_14_ECHO_FG.wav,C_14_CLIP_FG.wav,C_11_CLIP_FG.wav,C_01_NOISE_FG.wav,C_24_ECHO_FA.wav`
