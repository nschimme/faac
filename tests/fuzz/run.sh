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

# Build and run the libFuzzer harnesses with ASan+UBSan. Needs clang.
# usage: tests/fuzz/run.sh [build-dir] [seconds] [target...]   (build-dir must hold meson's config.h)
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

# Generated headers (git_version.h) and config.h come from the meson build.
[ -f "$bd/build.ninja" ] || { echo "$bd is not a configured meson build directory" >&2; exit 1; }
ninja -C "$bd" >/dev/null

out=${FUZZ_OUT:-$bd/fuzz}
mkdir -p "$out"
out=$(cd "$out" && pwd)

san=fuzzer,address,undefined
fuzz_detect_leaks=${FUZZ_DETECT_LEAKS:-1}
# The SSE2 quantizer kernel is x86-only; other targets use the scalar path.
case $(uname -m) in x86_64|i?86|amd64) sse=-msse2 ;; *) sse= ;; esac

# Helper function to compile a harness
build_target() {
    name=$1
    shift
    if [ -n "$targets" ]; then
        case " $targets " in *" $name "*) ;; *) return ;; esac
    fi
    echo "Building harness: $name"
    clang -g -O1 -fsanitize=$san -fno-sanitize-recover=undefined $sse "$@" -o "$out/$name"
}

# Source files for libfaad (excluding test_*.c)
faad_src="$root/libfaad/asc.c $root/libfaad/bits.c $root/libfaad/coupling.c $root/libfaad/decoder.c $root/libfaad/dequant.c $root/libfaad/huffman.c $root/libfaad/imdct.c $root/libfaad/ps.c $root/libfaad/sbr.c $root/libfaad/sbr_dec_tables.c $root/libfaad/stereo.c $root/libfaad/syntax.c $root/libfaad/tns.c"

# Source files for libfaam
faam_src="$root/libfaam/atom_patch.c $root/libfaam/chapter.c $root/libfaam/demux.c $root/libfaam/faam_util.c $root/libfaam/metadata.c $root/libfaam/mux.c $root/libfaam/tag.c"

# Source files for libfaac
faac_src="$root/libfaac/bitstream.c $root/libfaac/blockswitch.c $root/libfaac/channels.c $root/libfaac/cpu_compute.c $root/libfaac/faac.c $root/libfaac/filtbank.c $root/libfaac/frame.c $root/libfaac/huff2.c $root/libfaac/huffdata.c $root/libfaac/quantize.c $root/libfaac/ratecontrol.c $root/libfaac/stereo.c $root/libfaac/tns.c $root/libfaac/util.c"

# Match the Meson feature settings: an LC-only build leaves the SBR sources out (sbr.h has stubs),
# and PS is opt-in.
if grep -q '^#define FAAC_ENCODER_SBR 1$' "$bd/config.h"; then
    faac_src="$faac_src $root/libfaac/resample.c $root/libfaac/sbr_analysis.c $root/libfaac/sbr_bitstream.c $root/libfaac/sbr.c $root/libfaac/sbr_huff_tables.c"
fi
if grep -q '^#define FAAC_ENCODER_PS 1$' "$bd/config.h"; then
    faac_src="$faac_src $root/libfaac/ps.c $root/libfaac/ps_huff_tables.c"
fi

# Source files for libfaar
faar_src="$root/libfaar/polyresample.c $root/libfaar/dft_coeffs.c $root/libfaar/dft_prefilter.c $root/libfaar/dft_fir.c $root/libfaar/fft.c"

# Compile quantize_sse.c with gcc if available to bypass clang bug on immintrin.h
q_sse_obj=""
if [ -n "$sse" ]; then
    if gcc -O1 -msse2 -I"$root/include" -I"$root/common" -I"$root/libfaac" -include "$bd/config.h" -c "$root/libfaac/quantize_sse.c" -o "$out/q_sse.o" 2>/dev/null; then
        q_sse_obj="$out/q_sse.o"
    elif [ -f "$bd/libfaac/libquantize_sse.a" ]; then
        q_sse_obj="$bd/libfaac/libquantize_sse.a"
    fi
fi

# 1. fuzz_faar_resampler (libfaar streaming API)
build_target "fuzz_faar_resampler" \
    -include "$bd/config.h" -DFAAR_BUILDING -DFAAR_WIDE_HIGH \
    -I"$root/include" -I"$root/libfaar" \
    "$root/tests/fuzz/fuzz_faar_resampler.c" $faar_src -lm

# 2. fuzz_faac_encode
build_target "fuzz_faac_encode" \
    -include "$bd/config.h" -DCOMMON_PREFIX=faac_ -I"$root/include" -I"$root/common" -I"$root/libfaac" \
    "$root/tests/fuzz/fuzz_faac_encode.c" $faac_src $q_sse_obj \
    "$root/common/fft.c" "$root/common/sbr_tables.c" "$root/common/sfb_tables.c" -lm

# 2b. fuzz_faac_faad_pce: libfaac PCE (7.1) -> libfaad PCE decode
build_target "fuzz_faac_faad_pce" \
    -include "$bd/config.h" -DCOMMON_PREFIX=fuzz_ -I"$root/include" -I"$root/common" -I"$root/libfaac" -I"$root/libfaad" \
    "$root/tests/fuzz/fuzz_faac_faad_pce.c" $faac_src $faad_src $q_sse_obj $(ls "$root"/common/*.c 2>/dev/null) -lm

# 3. fuzz_frontend_wav
build_target "fuzz_frontend_wav" \
    -include "$bd/config.h" -I"$root/include" -I"$root/common" -I"$root/frontend" -I"$bd/frontend" \
    "$root/tests/fuzz/fuzz_frontend_wav.c" "$root/frontend/input.c" "$root/frontend/charset.c" \
    "$root/frontend/cli_io.c" -lm

# 4. fuzz_faad_adts
build_target "fuzz_faad_adts" \
    -include "$bd/config.h" -DCOMMON_PREFIX=faad_ -I"$root/include" -I"$root/common" -I"$root/libfaad" \
    "$root/tests/fuzz/fuzz_faad_adts.c" $faad_src $(ls "$root"/common/*.c 2>/dev/null) -lm

# RAW 960-sample (DAB+) frames
build_target "fuzz_faad_raw960" \
    -include "$bd/config.h" -DCOMMON_PREFIX=faad_ -I"$root/include" -I"$root/common" -I"$root/libfaad" \
    "$root/tests/fuzz/fuzz_faad_raw960.c" $faad_src $(ls "$root"/common/*.c 2>/dev/null) -lm

# 5. fuzz_faad_mp4 (with and without FAAM flags)
build_target "fuzz_faad_mp4" \
    -include "$bd/config.h" -DCOMMON_PREFIX=faad_ -I"$root/include" -I"$root/common" -I"$root/libfaad" -I"$root/libfaam" -I"$root/frontend" -I"$bd/frontend" \
    "$root/tests/fuzz/fuzz_faad_mp4.c" "$root/frontend/mp4read.c" "$root/frontend/encoder_quirks.c" \
    "$root/frontend/cli_io.c" "$root/frontend/charset.c" $faad_src $faam_src $(ls "$root"/common/*.c 2>/dev/null) -lm

build_target "fuzz_faad_mp4_full" \
    -include "$bd/config.h" -DCOMMON_PREFIX=faad_ -I"$root/include" -I"$root/common" -I"$root/libfaad" -I"$root/libfaam" -I"$root/frontend" -I"$bd/frontend" \
    -DFAAM_MUXER_FRAGMENTED=1 -DFAAM_MUXER_VIDEO=1 \
    "$root/tests/fuzz/fuzz_faad_mp4.c" "$root/frontend/mp4read.c" "$root/frontend/encoder_quirks.c" \
    "$root/frontend/cli_io.c" "$root/frontend/charset.c" $faad_src $faam_src $(ls "$root"/common/*.c 2>/dev/null) -lm

# 6. fuzz_faam_demux (with and without FAAM flags)
build_target "fuzz_faam_demux" \
    -include "$bd/config.h" -DCOMMON_PREFIX=fuzz_ -I"$root/include" -I"$root/common" -I"$root/libfaam" \
    "$root/tests/fuzz/fuzz_faam_demux.c" $faam_src $(ls "$root"/common/*.c 2>/dev/null) -lm

build_target "fuzz_faam_demux_full" \
    -include "$bd/config.h" -DCOMMON_PREFIX=fuzz_ -I"$root/include" -I"$root/common" -I"$root/libfaam" \
    -DFAAM_MUXER_FRAGMENTED=1 -DFAAM_MUXER_VIDEO=1 \
    "$root/tests/fuzz/fuzz_faam_demux.c" $faam_src $(ls "$root"/common/*.c 2>/dev/null) -lm

# 7. fuzz_faam_tags
build_target "fuzz_faam_tags" \
    -include "$bd/config.h" -DCOMMON_PREFIX=fuzz_ -I"$root/include" -I"$root/common" -I"$root/libfaam" \
    "$root/tests/fuzz/fuzz_faam_tags.c" $faam_src $(ls "$root"/common/*.c 2>/dev/null) -lm

# 8. fuzz_faam_roundtrip
build_target "fuzz_faam_roundtrip" \
    -include "$bd/config.h" -DCOMMON_PREFIX=fuzz_ -I"$root/include" -I"$root/common" -I"$root/libfaam" \
    -DFAAM_MUXER_FRAGMENTED=1 -DFAAM_MUXER_VIDEO=1 \
    "$root/tests/fuzz/fuzz_faam_roundtrip.c" $faam_src $(ls "$root"/common/*.c 2>/dev/null) -lm

# 9. fuzz_faam_annexb
build_target "fuzz_faam_annexb" \
    -include "$bd/config.h" -DCOMMON_PREFIX=fuzz_ -I"$root/include" -I"$root/common" -I"$root/frontend" -I"$root/libfaam" \
    -DFAAM_MUXER_FRAGMENTED=1 -DFAAM_MUXER_VIDEO=1 \
    "$root/tests/fuzz/fuzz_faam_annexb.c" "$root/frontend/annexb.c" $faam_src $(ls "$root"/common/*.c 2>/dev/null) -lm

# 10. fuzz_frontend_adts_header
build_target "fuzz_frontend_adts_header" \
    -include "$bd/config.h" -I"$root/include" -I"$root/common" -I"$root/frontend" \
    "$root/tests/fuzz/fuzz_frontend_adts_header.c" "$root/frontend/adts.c" -lm

echo "All fuzz targets compiled successfully."

if [ -n "$FUZZ_BUILD_ONLY" ]; then
    exit 0
fi

cd "$out"

all_targets="fuzz_faar_resampler fuzz_faac_encode fuzz_faac_faad_pce fuzz_frontend_wav fuzz_faad_adts fuzz_faad_raw960 fuzz_faad_mp4 fuzz_faad_mp4_full fuzz_faam_demux fuzz_faam_demux_full fuzz_faam_tags fuzz_faam_roundtrip fuzz_faam_annexb fuzz_frontend_adts_header"

if [ -z "$targets" ]; then
    run_list="$all_targets"
else
    run_list="$targets"
fi

# Seeds for the MP4 targets: random bytes almost never form a file with a track, so the
# tag-edit harness would never be held to its "still readable" rule without a real one.
seed_m4a="$out/seed.m4a"
seed_adts="$out/seed.aac"
seed_8ch_adts="$out/seed_8ch.aac"
seed_8ch_m4a="$out/seed_8ch.m4a"
if [ -x "$bd/frontend/faac" ] && [ ! -f "$seed_m4a" ]; then
    head -c 40000 /dev/zero | "$bd/frontend/faac" -P --title seed -o "$seed_m4a" - >/dev/null 2>&1 || rm -f "$seed_m4a"
fi
if [ -x "$bd/frontend/faac" ] && [ ! -f "$seed_adts" ]; then
    head -c 40000 /dev/zero | "$bd/frontend/faac" -P -o "$seed_adts" - >/dev/null 2>&1 || rm -f "$seed_adts"
fi
# Seven-plus-one channels exercise the program config element (PCE) path in
# both the encoder and the decoder.
if [ -x "$bd/frontend/faac" ] && [ ! -f "$seed_8ch_adts" ]; then
    head -c 300000 /dev/zero | "$bd/frontend/faac" -P -C 8 -R 48000 -a -o "$seed_8ch_adts" - >/dev/null 2>&1 || rm -f "$seed_8ch_adts"
fi
if [ -x "$bd/frontend/faac" ] && [ ! -f "$seed_8ch_m4a" ]; then
    head -c 300000 /dev/zero | "$bd/frontend/faac" -P -C 8 -R 48000 -o "$seed_8ch_m4a" - >/dev/null 2>&1 || rm -f "$seed_8ch_m4a"
fi
if [ -f "$seed_8ch_adts" ]; then
    python3 "$root/tests/fuzz/seed_faad_pce.py" "$seed_8ch_adts" "$out/pce_seed" || true
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
    if [ -f "$seed_8ch_m4a" ]; then
        case $t in
            fuzz_faad_mp4|fuzz_faad_mp4_full) cp "$seed_8ch_m4a" "corpus_$t/seed_8ch_m4a" ;;
        esac
    fi
    if [ -f "$out/pce_seed/pce8_adts" ]; then
        case $t in
            fuzz_faad_adts) cp "$out/pce_seed/pce8_adts" "corpus_$t/pce8_adts" ;;
            fuzz_faad_raw960) cp "$out/pce_seed/pce8_raw960" "corpus_$t/pce8_raw960" ;;
        esac
    fi
    if [ "$t" = fuzz_faad_adts ] && [ -f "$seed_adts" ]; then
        { printf '\001'; cat "$seed_adts"; } > "corpus_$t/seed_adts"
    fi
    max_len=65536
    case $t in
        fuzz_frontend_wav) max_len=4096 ;;
        fuzz_faac_encode)
            max_len=70000
            if grep -q '^#define FAAC_ENCODER_PS 1$' "$bd/config.h"; then
                python3 "$root/tests/fuzz/seed_faac_encode.py" "corpus_$t"
            fi ;;
        fuzz_faac_faad_pce)
            max_len=70000
            if [ ! -f "corpus_$t/seed_8ch" ]; then
                { printf '\001\003'; head -c 4096 /dev/zero | tr '\000' 'A'; } > "corpus_$t/seed_8ch"
                { printf '\005\003'; head -c 4096 /dev/zero | tr '\000' 'B'; } > "corpus_$t/seed_8ch_raw"
                { printf '\002\003'; head -c 4096 /dev/zero | tr '\000' 'C'; } > "corpus_$t/seed_6ch"
            fi ;;
        fuzz_faam_annexb)
            # A sample must outgrow the splitter's initial buffer to exercise its growth path,
            # which the default input limit cannot reach.
            max_len=300000
            if [ ! -f "corpus_$t/large_sample" ]; then
                { printf '\000\000\000\001\145'; head -c 200000 /dev/zero | tr '\000' 'A'; } > "corpus_$t/large_sample"
            fi ;;
    esac
    echo "Running $t for $secs seconds..."
    ASAN_OPTIONS=detect_leaks=$fuzz_detect_leaks:halt_on_error=1:abort_on_error=1 \
    UBSAN_OPTIONS=halt_on_error=1:abort_on_error=1 \
    "./$t" -max_total_time="$secs" -max_len=$max_len -rss_limit_mb=2048 -timeout=10 "corpus_$t"
done
