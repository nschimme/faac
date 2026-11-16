# libxaac ("Ittiam AAC") 48 kHz references

`xaacenc` from faac-benchmark `bin/`, argv from `IttiamAACEncoder.get_encode_cmd` (`-br -aot:{5|29} -adts:1`), faac-benchmark 9f5f102, same 77 clips as `../apple-48k/`.
Layout `{he,hev2}/<kbps>k/<clip>.wav.m4a` (the files carry ADTS despite the extension, as the benchmark writes them); HE 32-64, HE-v2 24-48. 539 streams, strict-decode clean, 48 kHz.
LC is not included: libxaac trails Apple and FAAC at LC. It leads FAAC at HE-v2 24k/32k (mean and P1) and HE-v1 P1 at 40-48k. Black-box reference only.
