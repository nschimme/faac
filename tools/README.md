# PS quality investigation tools

These scripts use the sibling `faac-benchmark` checkout for scoring. Set paths
for the local checkout, benchmark corpus, FAAC build, and optional decoder tools:

```sh
export BENCHMARK_DIR=/path/to/faac-benchmark
export FAAC_BENCHMARK_DIR="$BENCHMARK_DIR"
export CORPUS="$BENCHMARK_DIR/data/external/audio"
export BENCH_PYTHON="$BENCHMARK_DIR/.venv/bin/python"
export FAAC=/path/to/faac/frontend/faac
export FAAD=/path/to/faac/frontend/faad
export NUMBA_CACHE_DIR=/tmp/faac-numba-cache
```

Run the metric scripts with the benchmark virtual environment. Outputs and
encoded audio should go under `/tmp` or another results directory, not into
the source tree.

## Forced-profile FAAC sweep

`ps_quality_ab.py` runs forced HE-AAC v1/v2 encodes and records payload rate,
FFmpeg MOS, and stereo coherence. `-b` is the total stereo target bitrate.
For example, one candidate at 16 kb/s across the corpus:

```sh
"$BENCH_PYTHON" tools/ps_quality_ab.py \
  --encoder candidate="$FAAC" \
  --clips "$CORPUS"/*.wav \
  --profiles he-aac-v1,he-aac-v2 --bitrates 16 \
  --output-dir /tmp/faac-ps-candidate
```

The equivalent direct encoder options for an individual clip are:

```sh
"$FAAC" -b 16 --object-type he-aac-v1 -o /tmp/clip-v1.m4a input.wav
"$FAAC" -b 16 --object-type he-aac-v2 -o /tmp/clip-v2.m4a input.wav
```

## FAAD decision dump

Build FAAD with Meson `-Dstats=true`. Set `FAAD_DUMP` to a writable text file
while decoding; records are appended. Then summarize core window use, PS IID,
ICC, phase modes, envelope counts, and syntax bit estimates:

```sh
FAAD_DUMP=/tmp/clip-faad-dump.txt "$FAAD" --overwrite -o /tmp/clip-decoded.wav input.m4a
"$BENCH_PYTHON" tools/ps_dump_summary.py /tmp/clip-faad-dump.txt
```

## FDK HE-AAC comparison and gapless normalization

The harness measures AAC payload separately from ADTS transport overhead. It
queries FDK's configured delay and frame length, derives trailing padding from
the input PCM and ADTS frame count, and muxes a gapless M4A with FAAM before
scoring. It preserves the raw `.aac` and normalized `.m4a` artifacts.

Compile the helper with the installed FDK AAC development package, then run the
same corpus through `aac-enc` and FAAM:

```sh
export FDK_INFO=/tmp/ps_fdk_info
cc $(pkg-config --cflags fdk-aac) tools/ps_fdk_info.c \
  $(pkg-config --libs fdk-aac) \
  -Wl,-rpath,$(pkg-config --variable=libdir fdk-aac) -o "$FDK_INFO"
export AAC_ENC=/path/to/aac-enc
export FAAM=/path/to/faam

"$BENCH_PYTHON" tools/ps_quality_ab.py \
  --encoder fdk="$AAC_ENC" --encoder-kind fdk=fdk \
  --faam "$FAAM" --fdk-info "$FDK_INFO" \
  --clips "$CORPUS"/*.wav --profiles he-aac-v2 --bitrates 8,12,16 \
  --output-dir /tmp/faac-ps-fdk
```

For a saved raw FDK manifest, `ps_fdk_gapless_rescore.py` remuxes and rescores
without re-encoding. It accepts `--bitrates 8,12,16`, `--jobs N`, and an
optional `--decoder PATH` for an additional decoded comparison.

```sh
"$BENCH_PYTHON" tools/ps_fdk_gapless_rescore.py /tmp/faac-ps-fdk/results.json \
  --output /tmp/faac-ps-fdk-gapless --faam "$FAAM" --fdk-info "$FDK_INFO" \
  --bitrates 8,12,16 --jobs 3
```

## PCHIP BD-rate

`ps_bd_rate.py` pairs clips and sample rates, compares quality over achieved
payload bitrates, and reports per-clip PCHIP BD-rate. It requires at least
three raw rungs per arm, with at least two points surviving its monotonicity
filter, and does not extrapolate beyond the shared quality interval. For example:

```sh
"$BENCH_PYTHON" tools/ps_bd_rate.py --benchmark-dir "$BENCHMARK_DIR" \
  --baseline /tmp/faac-ps-baseline/results.json \
  --candidate /tmp/faac-ps-candidate/results.json \
  --baseline-label baseline --candidate-label candidate \
  --baseline-profile he-aac-v1 --candidate-profile he-aac-v2 \
  --bitrates 8,12,16,20,24 --output /tmp/faac-ps-bd-rate.json
```
