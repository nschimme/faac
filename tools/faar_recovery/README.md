# FAAR recovery diagnostics

These tools provide independent common-rate benchmarking and quality diagnostics for FAAR. The production Meson default is `staged-short`; planned common routes use its DFT-plus-phase pipeline, while unplanned pairs retain a compatibility fallback. `polyphase` and `staged` remain comparison build options. The checked-in current snapshot covers 16 common conversions and records performance, passband, transient, and image probes.

## Build

Requirements: GCC/G++, CMake, Ninja, Python + NumPy, Valgrind, GNU binutils, and
local pinned SoX/KBSRC checkouts. The ARDFTSRC source adapter is included here;
its external AudioFile, Eigen, and FFTW headers/libraries must be supplied.
The defaults reuse the existing `/tmp` reference checkouts and extracted Debian
FFTW/Eigen development files. To obtain source checkouts use `gh repo clone`,
then check out the revisions in `reference-notices/NOTICE.md`.

```sh
python3 tools/faar_recovery/build.py --root /tmp/faar-recovery \
  --sox-source /tmp/sox-vulkan-audit --kbsrc-source /tmp/kbsrc \
  --audiofile-include /tmp/ARDFTSRC-AudioFile \
  --ardft-deps /tmp/ardft-deps/root/usr
```

The build creates three isolated double-coefficient FAAR variants: default
polyphase, the archived scaled-cutoff regression, and reconstructed staged
baseline with fixed 0.49 cycles/input-sample output cutoff. It refuses an
unrecognized cutoff configuration. It also builds native reference drivers.
External source instrumentation is confined to scratch copies; numerical code
and reference checkouts are unchanged. All sources, commands, and binaries are
identified in `build-manifest.json`. KBSRC uses a portable build with both
baseline and AVX2 dispatch; the existing native-only build cannot test its
portable backend. Its coefficient designer may spawn workers despite the
one-thread processing option, and that CPU is included in setup timing.

## Run serially

```sh
python3 tools/faar_recovery/diagnose.py --phase all
python3 tools/faar_recovery/oracle.py
python3 tools/faar_recovery/stage_timers.py
python3 tools/faar_recovery/profile.py --method callgrind
python3 tools/faar_recovery/profile.py --method massif
/tmp/faar-recovery/fft-bench > /tmp/faar-recovery/fft.json
python3 tools/faar_recovery/validate.py
python3 tools/faar_recovery/matched_kbsrc.py
```

`diagnose.py` supports `--phase baseline|signals|bench`, `--engines`, and an
optional `--reference` executable. Measurements default to seven alternating
serial rounds after one warmup on common approximately 30-second stereo input.
A common zero tail aligns the benchmark workload across engines. Setup, native
processing, whole child CPU, and whole child wall time are distinguished.
SoX's `bufsiz` is in samples: the driver sets `block_frames * channels`, not
bytes. The `sox-wide` reference uses CPU `rate -v -b 99.5`.
An experimental candidate library can be included with
`--engines faar-candidate sox-wide ardftsrc --candidate-library PATH`; use
`--suffix=-staged-short` to keep its signal and benchmark JSON separate from
the baseline records.

Quality probes use common float32 PCM and output, with the same interior time
window, recorded ARDFTSRC delay removal, and zero endpoints. Extra precision is
internal and recorded. Sample fingerprints are not cross-engine quality tests.
The two-tone control reports selected IMD product amplitudes; multitone/noise
and endpoint impulse records remain diagnostics rather than composite scores.

The 70-digit Decimal oracle checks SoX prototype generation, full phase-bank
ordering, and selected interior cascade impulse samples against observed output.
The stage timers wrap only processing boundaries in a scratch FAAR library;
outputs must match the uninstrumented library byte for byte before timing.
FFT microbenchmarks include necessary packing and inverse normalization and
validate round trips before timing. The current FAAR real FFT rejects non-power
of-two lengths; rational-size tests use its existing complex fallback explicitly.

Callgrind uses **processing-only** collection and reports instruction counts,
not CPU time. Massif reports peak heap including driver input/output storage;
it is not a streaming-memory comparison. Native FAAR allocation accounting
includes arena allocations **and mmap allocations**, and reports sampled retained
streaming high water separately from transient peak allocations. KBSRC's zero
`setup_live_heap_bytes` field means unavailable, not zero memory consumption.
`validation.json` contains exact adapter/CLI comparisons and binary size data.

The bounded KBSRC sweep derives lengths from the reference 220.5 Hz transition
and tests beta 16, 20, 24 in portable Fast mode; it is a reference-setting
comparison, not a change to FAAR.

Tone diagnostics can target explicit pairs with `--signal-pair IN OUT`. For a
full response sweep, `--signal-matrix --tones-only` runs the 357 ordered rate
and +/-1 Hz neighbor pairs in FAAR's matrix fixture while skipping impulse,
two-tone, and noise cases. For example:

```sh
python3 tools/faar_recovery/diagnose.py --phase signals --signal-matrix \
  --tones-only --engines faar-candidate ardftsrc \
  --candidate-library /tmp/faac-faar-native-build/libfaar/libfaar.so \
  --faar-quality 2 --suffix=-response-matrix-high
```

Upsampling image measurements use a separate least-squares sinusoid fit on a
one-second steady-state output segment. `--image-pair IN OUT` measures one
upsampling conversion; `--image-matrix` covers supported upsampling pairs and
their +/-1 Hz neighbors. The input tone is 0.45 times the input rate (the
documented High passband edge), and all in-band images separated from the
wanted tone by at least 10 Hz are recorded. Run `--faar-quality 2`, `1`, and
`0` to characterize High, Good, and Fast respectively. ARDFTSRC uses its fixed
reference settings for all three runs; lower-preset image records are
characterization, not a pass/fail comparison to an equivalent preset. No
Good/Fast image acceptance mask is currently specified.
Both linear amplitudes and attenuation relative to the wanted tone are kept so
results near the float32 floor are not mistaken for precise dB measurements.
Non-tone signal runs also record central-impulse peak alignment and pre/post
ringing energy outside a 1 ms guard around the main pulse. These are descriptive
measurements, not automatic acceptance thresholds. `--transient-matrix` runs a
central impulse and endpoint-energy probe over the 357 ordered supported-rate
and +/-1 Hz neighbor pairs. It uses a two-second input with impulses at the
start, center, and end; the diagnostic FAAR driver explicitly selects ZERO
boundary extension. The output records per-pair symmetry, peak alignment, and
10 ms endpoint energy. It does not compare HOLD-boundary behavior or define an
automatic symmetry/endpoint threshold.
Example:

```sh
python3 tools/faar_recovery/diagnose.py --phase signals --image-matrix \
  --engines faar-candidate ardftsrc \
  --candidate-library /tmp/faac-faar-native-build/libfaar/libfaar.so \
  --faar-quality 2 --suffix=-image-matrix-high

python3 tools/faar_recovery/diagnose.py --phase signals --image-matrix \
  --engines faar-candidate ardftsrc \
  --candidate-library /tmp/faac-faar-native-build/libfaar/libfaar.so \
  --faar-quality 1 --suffix=-image-matrix-good

python3 tools/faar_recovery/diagnose.py --phase signals --image-matrix \
  --engines faar-candidate ardftsrc \
  --candidate-library /tmp/faac-faar-native-build/libfaar/libfaar.so \
  --faar-quality 0 --suffix=-image-matrix-fast

python3 tools/faar_recovery/diagnose.py --phase signals --transient-matrix \
  --engines faar-candidate ardftsrc \
  --candidate-library /tmp/faac-faar-native-build/libfaar/libfaar.so \
  --faar-quality 2 --suffix=-transient-matrix-high
```

The proposed numerical-floor ceiling is diagnostic output for review; the
existing gain, alias, and fingerprint thresholds are unchanged. A whole-file
KBSRC/ARDFTSRC interface is never labeled as passing FAAR's streaming contract.
Reference licenses and instrumented-copy policies are in
[reference-notices/NOTICE.md](reference-notices/NOTICE.md).
