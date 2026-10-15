# Fixed steady FIXFIX envelope-count controls

Each worktree changes exactly one line in `libfaac/sbr_analysis.c`'s non-transient FIXFIX branch (`int ne = 1` or `int ne = 2`). The transient VARFIX branch remains at two envelopes. `SbrUpdate` stream bitrate thresholds and all header gates remain at their branch baselines; the stack retains limiter header gating from its source commit. This makes the controls unconditional steady 1 or 2 envelopes, independent of rung.

All four builds use Meson/Ninja release, frontend enabled, dynamic library, max-channels 8, SBR decimation 1, stats disabled. Builds completed successfully.

| Control | Worktree and source base | Frontend SHA-256 | libfaac SHA-256 |
|---|---|---|---|
| base-fixed1 | `/private/tmp/faac-envcount-fixed1-base`, `99ce2b0980180194f9ccd1993a786208d277cf3e` | `c0dc1e7420c70657307af24d866786c3d15e37fee919834d43cd37157d7529b9` | `6c1c5ccb22876e881b4704608d5a475f133f0902058426d8260e4db1d7f4b5e5` |
| base-fixed2 | `/private/tmp/faac-envcount-fixed2-base`, `99ce2b0980180194f9ccd1993a786208d277cf3e` | `c0dc1e7420c70657307af24d866786c3d15e37fee919834d43cd37157d7529b9` | `d74ddbfb2ef5d99f60cc2f0b54deb50004a10dfdfac213f48946f2eca9bb37bb` |
| stack-fixed1 | `/private/tmp/faac-envcount-fixed1-stack`, `5d1bd74703657ae6e9acc5fae1df63ce07190071` | `7741458a69d12c7ceabd7e0b2b3e60f85f74620f29a28c7be49fb2f1c6b32d5d` | `e8069b77feeb21cf7dfa5a950bd63a92e398bc0a579c084c0e471562cea04d94` |
| stack-fixed2 | `/private/tmp/faac-envcount-fixed2-stack`, `5d1bd74703657ae6e9acc5fae1df63ce07190071` | `7741458a69d12c7ceabd7e0b2b3e60f85f74620f29a28c7be49fb2f1c6b32d5d` | `5cd28bf8ba48da6ec7ec60bd119fdf1b5e954fb979fd7480cf426138252db700` |

One-line patches are stored beside this file. Full 49-clip scoring and strict FAAD3/FFmpeg validation are archived at `full/`; see `full/RESULTS.md` for matched PCHIP comparisons.
