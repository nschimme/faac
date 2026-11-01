#!/bin/sh
# FAAC - Freeware Advanced Audio Coder
# Copyright (C) 2026 Nils Schimmelmann
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.

# Long libFuzzer sessions over the Meson-built harnesses. Needs clang.
# Configure the build directory with
#   CC=clang meson setup <build> -Db_sanitize=address,undefined -Db_lundef=false \
#       -Dc_args=-fsanitize=fuzzer-no-link -Dfuzzing-engine=-fsanitize=fuzzer
# usage: tests/fuzz/run.sh [build-dir] [seconds] [target...]
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
bd=${1:-$root/build}
case $bd in
    /*) ;;
    *) bd=$root/$bd ;;
esac
secs=${2:-60}
if [ "$#" -ge 2 ]; then
    shift 2
else
    shift "$#"
fi
targets="$*"

[ -f "$bd/build.ninja" ] || { echo "$bd is not a configured meson build directory" >&2; exit 1; }
ninja -C "$bd" tests/fuzz/corpus.stamp all >/dev/null

out=${FUZZ_OUT:-$bd/fuzz}
mkdir -p "$out"
out=$(cd "$out" && pwd)

# libFuzzer's own RSS thread trips LeakSanitizer on macOS.
case $(uname) in
    Darwin) fuzz_detect_leaks=${FUZZ_DETECT_LEAKS:-0} ;;
    *) fuzz_detect_leaks=${FUZZ_DETECT_LEAKS:-1} ;;
esac
fuzz_bin="$bd/tests/fuzz"
corpus_src="$fuzz_bin/corpus"

[ -n "$targets" ] || targets=$(cd "$fuzz_bin" && ls -d fuzz_* 2>/dev/null | grep -v '\.' || true)
[ -n "$targets" ] || { echo "no fuzz harnesses built in $fuzz_bin" >&2; exit 1; }

cd "$out"

for t in $targets; do
    [ -x "$fuzz_bin/$t" ] || { echo "Target $t not built in $fuzz_bin!" >&2; exit 1; }
    # Without an engine the harness only replays files and ignores -max_total_time.
    "$fuzz_bin/$t" -runs=0 2>&1 | grep -q 'INFO: Seed:' || { echo "$t was built without -Dfuzzing-engine" >&2; exit 1; }
    mkdir -p "corpus_$t"
    cp -n "$corpus_src/${t#fuzz_}"/* "corpus_$t"/ 2>/dev/null || true
    max_len=65536
    case $t in
        fuzz_frontend_wav) max_len=4096 ;;
        fuzz_faac_encode|fuzz_faac_faad_pce) max_len=70000 ;;
        # A sample must outgrow the splitter's initial buffer to exercise its growth path.
        fuzz_faam_annexb) max_len=300000 ;;
    esac
    echo "Running $t for $secs seconds..."
    ASAN_OPTIONS=detect_leaks=$fuzz_detect_leaks:halt_on_error=1:abort_on_error=1 \
    UBSAN_OPTIONS=halt_on_error=1:abort_on_error=1 \
    "$fuzz_bin/$t" -max_total_time="$secs" -max_len=$max_len -rss_limit_mb=2048 \
        -timeout=10 -artifact_prefix="$out/" "corpus_$t"
done
