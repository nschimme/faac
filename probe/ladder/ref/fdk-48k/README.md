# FDK AAC 48 kHz references

fdkaac 1.0.9 (Homebrew), argv from `FDKAACEncoder.get_encode_cmd` (`fdkaac -p {2|5|29} -b BPS -o OUT IN`), faac-benchmark 9f5f102, same 77 full-length clips as `../apple-48k/`.
Layout `{lc,he,hev2}/<kbps>k/<clip>.wav.m4a`; rungs LC 128-320, HE 32-64, HE-v2 24-48. 924 streams, all strict-decode clean, all 48 kHz. Realized rates run 1-2 % over nominal.
Replaces the old 49-clip `ref/fdk/`. Targets: FDK leads stereo fidelity overall and HE-v1 P1 at 40-64k. Black-box reference only.
