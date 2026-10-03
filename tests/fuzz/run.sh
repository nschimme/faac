#!/bin/sh
# Build and run the libFuzzer harnesses with ASan+UBSan. Needs clang.
# usage: tests/fuzz/run.sh [build-dir] [seconds]   (build-dir must hold meson's config.h)
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
bd=${1:-$root/build}
secs=${2:-60}
out=${FUZZ_OUT:-$bd/fuzz}
mkdir -p "$out/corpus"
san=fuzzer,address,undefined
clang -g -O1 -fsanitize=$san -fno-sanitize-recover=undefined -msse2 \
    -include "$bd/config.h" -I"$root/include" -I"$root/common" -I"$root/libfaac" \
    "$root/tests/fuzz/fuzz_encode.c" "$root"/libfaac/*.c $(ls "$root"/common/*.c 2>/dev/null) -lm \
    -o "$out/fuzz_encode"
clang -g -O1 -fsanitize=$san -fno-sanitize-recover=undefined \
    -include "$bd/config.h" -I"$root/include" -I"$root/common" -I"$root/frontend" -I"$bd/frontend" \
    "$root/tests/fuzz/fuzz_wav.c" "$root/frontend/input.c" "$root/frontend/charset.c" \
    $(ls "$root/frontend/cli_common.c" 2>/dev/null) -lm -o "$out/fuzz_wav"
cd "$out"
mkdir -p corpus_wav
./fuzz_encode -max_total_time="$secs" -max_len=70000 -jobs=4 -workers=4 corpus
./fuzz_wav -max_total_time="$secs" -max_len=4096 -jobs=4 -workers=4 corpus_wav
