#!/bin/sh
# Build and run the libFuzzer harnesses with ASan+UBSan. Needs clang.
# usage: tests/fuzz/run.sh [build-dir] [seconds] [target...]   (build-dir must hold meson's config.h)
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
bd=${1:-$root/build}
secs=${2:-60}
shift 2 2>/dev/null || true
targets="$*"

# Generated headers (git_version.h) and config.h come from the meson build.
[ -f "$bd/build.ninja" ] || { echo "$bd is not a configured meson build directory" >&2; exit 1; }
ninja -C "$bd" >/dev/null

out=${FUZZ_OUT:-$bd/fuzz}
mkdir -p "$out"

san=fuzzer,address,undefined
# The SSE2 quantizer kernel is x86-only; other targets use the scalar path.
case $(uname -m) in x86_64|i?86|amd64) sse=-msse2 ;; *) sse= ;; esac

# Helper function to compile a harness
build_target() {
    name=$1
    shift
    echo "Building harness: $name"
    clang -g -O1 -fsanitize=$san -fno-sanitize-recover=undefined $sse "$@" -o "$out/$name"
}

# Source files for libfaad (excluding test_*.c)
faad_src="$root/libfaad/asc.c $root/libfaad/bits.c $root/libfaad/decoder.c $root/libfaad/dequant.c $root/libfaad/huffman.c $root/libfaad/imdct.c $root/libfaad/ps.c $root/libfaad/sbr.c $root/libfaad/sbr_dec_tables.c $root/libfaad/stereo.c $root/libfaad/syntax.c $root/libfaad/tns.c"

# Source files for libfaam
faam_src="$root/libfaam/atom_patch.c $root/libfaam/chapter.c $root/libfaam/demux.c $root/libfaam/faam_util.c $root/libfaam/metadata.c $root/libfaam/mux.c $root/libfaam/tag.c"

# Source files for libfaac
faac_src="$root/libfaac/bitstream.c $root/libfaac/blockswitch.c $root/libfaac/channels.c $root/libfaac/cpu_compute.c $root/libfaac/faac.c $root/libfaac/filtbank.c $root/libfaac/frame.c $root/libfaac/huff2.c $root/libfaac/huffdata.c $root/libfaac/quantize.c $root/libfaac/ratecontrol.c $root/libfaac/resample.c $root/libfaac/sbr_analysis.c $root/libfaac/sbr_bitstream.c $root/libfaac/sbr.c $root/libfaac/sbr_huff_tables.c $root/libfaac/stereo.c $root/libfaac/tns.c $root/libfaac/util.c"

# Compile quantize_sse.c with gcc if available to bypass clang bug on immintrin.h
q_sse_obj=""
if [ -n "$sse" ]; then
    if gcc -O1 -msse2 -I"$root/include" -I"$root/common" -I"$root/libfaac" -include "$bd/config.h" -c "$root/libfaac/quantize_sse.c" -o "$out/q_sse.o" 2>/dev/null; then
        q_sse_obj="$out/q_sse.o"
    elif [ -f "$bd/libfaac/libquantize_sse.a" ]; then
        q_sse_obj="$bd/libfaac/libquantize_sse.a"
    fi
fi

# 1. fuzz_encode
build_target "fuzz_encode" \
    -include "$bd/config.h" -DCOMMON_PREFIX=faac_ -I"$root/include" -I"$root/common" -I"$root/libfaac" \
    "$root/tests/fuzz/fuzz_encode.c" $faac_src $q_sse_obj \
    "$root/common/fft.c" "$root/common/sbr_tables.c" "$root/common/sfb_tables.c" -lm

# 2. fuzz_wav
build_target "fuzz_wav" \
    -include "$bd/config.h" -I"$root/include" -I"$root/common" -I"$root/frontend" -I"$bd/frontend" \
    "$root/tests/fuzz/fuzz_wav.c" "$root/frontend/input.c" "$root/frontend/charset.c" \
    $(ls "$root/frontend/cli_common.c" 2>/dev/null) -lm

# 3. fuzz_faad_adts
build_target "fuzz_faad_adts" \
    -include "$bd/config.h" -DCOMMON_PREFIX=faad_ -I"$root/include" -I"$root/common" -I"$root/libfaad" \
    "$root/tests/fuzz/fuzz_faad_adts.c" $faad_src $(ls "$root"/common/*.c 2>/dev/null) -lm

# 4. fuzz_faad_mp4 (with and without FAAM flags)
build_target "fuzz_faad_mp4" \
    -include "$bd/config.h" -DCOMMON_PREFIX=faad_ -I"$root/include" -I"$root/common" -I"$root/libfaad" -I"$root/libfaam" -I"$root/frontend" -I"$bd/frontend" \
    "$root/tests/fuzz/fuzz_faad_mp4.c" "$root/frontend/mp4read.c" $faad_src $faam_src $(ls "$root"/common/*.c 2>/dev/null) -lm

build_target "fuzz_faad_mp4_full" \
    -include "$bd/config.h" -DCOMMON_PREFIX=faad_ -I"$root/include" -I"$root/common" -I"$root/libfaad" -I"$root/libfaam" -I"$root/frontend" -I"$bd/frontend" \
    -DFAAM_MUXER_FRAGMENTED=1 -DFAAM_MUXER_VIDEO=1 \
    "$root/tests/fuzz/fuzz_faad_mp4.c" "$root/frontend/mp4read.c" $faad_src $faam_src $(ls "$root"/common/*.c 2>/dev/null) -lm

# 5. fuzz_faam_demux (with and without FAAM flags)
build_target "fuzz_faam_demux" \
    -include "$bd/config.h" -DCOMMON_PREFIX=fuzz_ -I"$root/include" -I"$root/common" -I"$root/libfaam" \
    "$root/tests/fuzz/fuzz_faam_demux.c" $faam_src $(ls "$root"/common/*.c 2>/dev/null) -lm

build_target "fuzz_faam_demux_full" \
    -include "$bd/config.h" -DCOMMON_PREFIX=fuzz_ -I"$root/include" -I"$root/common" -I"$root/libfaam" \
    -DFAAM_MUXER_FRAGMENTED=1 -DFAAM_MUXER_VIDEO=1 \
    "$root/tests/fuzz/fuzz_faam_demux.c" $faam_src $(ls "$root"/common/*.c 2>/dev/null) -lm

# 6. fuzz_faam_tags
build_target "fuzz_faam_tags" \
    -include "$bd/config.h" -DCOMMON_PREFIX=fuzz_ -I"$root/include" -I"$root/common" -I"$root/libfaam" \
    "$root/tests/fuzz/fuzz_faam_tags.c" $faam_src $(ls "$root"/common/*.c 2>/dev/null) -lm

# 7. fuzz_faam_roundtrip
build_target "fuzz_faam_roundtrip" \
    -include "$bd/config.h" -DCOMMON_PREFIX=fuzz_ -I"$root/include" -I"$root/common" -I"$root/libfaam" \
    -DFAAM_MUXER_FRAGMENTED=1 -DFAAM_MUXER_VIDEO=1 \
    "$root/tests/fuzz/fuzz_faam_roundtrip.c" $faam_src $(ls "$root"/common/*.c 2>/dev/null) -lm

# 8. fuzz_faam_annexb
build_target "fuzz_faam_annexb" \
    -include "$bd/config.h" -DCOMMON_PREFIX=fuzz_ -I"$root/include" -I"$root/common" -I"$root/frontend" -I"$root/libfaam" \
    -DFAAM_MUXER_FRAGMENTED=1 -DFAAM_MUXER_VIDEO=1 \
    "$root/tests/fuzz/fuzz_faam_annexb.c" "$root/frontend/annexb.c" $faam_src $(ls "$root"/common/*.c 2>/dev/null) -lm

# 9. fuzz_adts_header
build_target "fuzz_adts_header" \
    -include "$bd/config.h" -I"$root/include" -I"$root/common" -I"$root/frontend" \
    "$root/tests/fuzz/fuzz_adts_header.c" "$root/frontend/adts.c" -lm

echo "All fuzz targets compiled successfully."

if [ -n "$FUZZ_BUILD_ONLY" ]; then
    exit 0
fi

cd "$out"

all_targets="fuzz_encode fuzz_wav fuzz_faad_adts fuzz_faad_mp4 fuzz_faad_mp4_full fuzz_faam_demux fuzz_faam_demux_full fuzz_faam_tags fuzz_faam_roundtrip fuzz_faam_annexb fuzz_adts_header"

if [ -z "$targets" ]; then
    run_list="$all_targets"
else
    run_list="$targets"
fi

# Seeds for the MP4 targets: random bytes almost never form a file with a track, so the
# tag-edit harness would never be held to its "still readable" rule without a real one.
seed_m4a="$out/seed.m4a"
if [ -x "$bd/frontend/faac" ] && [ ! -f "$seed_m4a" ]; then
    head -c 40000 /dev/zero | "$bd/frontend/faac" -P --title seed -o "$seed_m4a" - >/dev/null 2>&1 || rm -f "$seed_m4a"
fi

for t in $run_list; do
    if [ ! -f "./$t" ]; then
        echo "Target $t not found!"
        exit 1
    fi
    mkdir -p "corpus_$t"
    if [ -f "$seed_m4a" ]; then
        case $t in
            fuzz_faam_tags) { printf '\000'; cat "$seed_m4a"; } > "corpus_$t/seed_m4a" ;;
            fuzz_faam_demux|fuzz_faam_demux_full|fuzz_faad_mp4|fuzz_faad_mp4_full) cp "$seed_m4a" "corpus_$t/seed_m4a" ;;
        esac
    fi
    max_len=65536
    case $t in
        fuzz_wav) max_len=4096 ;;
        fuzz_encode) max_len=70000 ;;
        fuzz_faam_annexb)
            # A sample must outgrow the splitter's initial buffer to exercise its growth path,
            # which the default input limit cannot reach.
            max_len=300000
            if [ ! -f "corpus_$t/large_sample" ]; then
                { printf '\000\000\000\001\145'; head -c 200000 /dev/zero | tr '\000' 'A'; } > "corpus_$t/large_sample"
            fi ;;
    esac
    echo "Running $t for $secs seconds..."
    ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:abort_on_error=1 \
    UBSAN_OPTIONS=halt_on_error=1:abort_on_error=1 \
    "./$t" -max_total_time="$secs" -max_len=$max_len -rss_limit_mb=2048 -timeout=10 "corpus_$t"
done
