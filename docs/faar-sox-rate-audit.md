# SoX rate converter source audit

## Pinned reference

The inspected revision is [simonefil/sox-vulkan](https://github.com/simonefil/sox-vulkan)
tag `sox-16.2.2`, commit
`54102e40ffbdbe991505b9ac0a7c62e0b4d4ae3c`, retrieved with `gh repo clone`.
Its CPU planner owns stage topology, quality, phase, bandwidth, aliasing,
length, latency, and drain behavior. The optional Vulkan backend executes
supported DFT, polyphase, and half-band stages, with CPU fallback for
unsupported plans. This is the candidate CPU reference because its history
contains post-14.4.2 rate work; the SourceForge release remains an independent
LGPL comparison baseline, not an assumed performance-equivalent substitute.

For provenance, the fork's history traces to upstream SoX and contains Rob
Sykes's 2012 small-factor downsampling optimization (`15bd4c16`, approximately
15% for 48 kHz to 44.1 kHz). The later fork includes additional CPU planner and
filter changes. We will compare both SoX CPU executables during benchmarking
so the throughput target reflects the more demanding CPU path.

The [comparison tool](../tools/compare_faar_sox_vulkan.py) runs FAAR against
the SoX CPU path and the `fast`, `precise`, and `reference` Vulkan profiles.
It is diagnostic tooling and does not qualify FAAR as a Vulkan implementation.

## File-level license audit

The SoX CPU rate implementation and filter templates carry LGPL-2.1-or-later
notices:

- `src/rate.c`
- `src/rate_filters.h`, `src/rate_poly_fir0.h`, `src/rate_poly_fir.h`,
  `src/rate_half_fir.h`
- `src/dft_filter.c`, `src/dft_filter.h`, `src/fifo.h`, and `src/fft4g.h`

`src/fft4g.c` carries Takuya Ooura's notice allowing use, modification, and
distribution for any purpose, including commercial use, with a requirement to
refer to the package when modifying the code. `src/sox_i.h` and `src/sox.h`
state that they are freely redistributable for any purpose with their notices
retained. The repository's `COPYING` explains that `sox.c` is GPL while
libSoX files are LGPL.

Ooura's [upstream FFT package page](https://www.kurims.kyoto-u.ac.jp/~ooura/fft.html)
has an explicit License section with the same permission statement (checked
2026-10-18). This is a custom permissive license, not an absent license or a
public-domain dedication. Any import should retain the copyright and full
permission statement, identify the upstream package, and record modifications.
This permission covers Ooura's code; SoX wrappers and later changes still need
their own file-level audit.

This fork added GPL-2.0-or-later diagnostics to its CPU files in commit
`6d2d7dbe`: `rate.c` unconditionally includes `src/diagnostics.h` and calls its
API; `dft_filter.c` does the same. Although these files retain LGPL headers,
those GPL diagnostics hooks and header are not cleared for FAAR. The parent
commit `c6b828f7566263135de81399d1c708d7432883e9` predates that diagnostics
change and its rate and DFT filter files have no diagnostics dependency. That
snapshot is the preferred adaptation point, subject to separating all
`HAVE_VULKAN` code and completing the dependency audit before copying. The
direct Vulkan rate files
and headers (`rate_vulkan*`, `rate_polyphase_vulkan*`, `rate_cubic_vulkan*`,
`rate_dsd_vulkan*`) are GPL-2.0-or-later and are excluded from the CPU port.
No SoX implementation code has been copied into libfaar.

The official [SourceForge SoX 14.4.2 release](https://sourceforge.net/projects/sox/files/sox/14.4.2/)
is also an LGPL-2.1-or-later source reference. Its source archive is listed with
SHA-256 `b45f598643ffbd8e363ff24d61166ccec4836fea6d3888881b8df53e3bb55f6c`
by the Fossies archive index. Debian's
`14.4.2+git20190427-1` `src/rate.c` is available at
[Debian Sources](https://sources.debian.org/src/sox/14.4.2%2Bgit20190427-1/src/rate.c)
and is the locally inspected 697-line upstream-derived CPU implementation.
The named templates, DFT filter, FIFO, FFT interface, FFT implementation, and
internal headers in the fork snapshot carry LGPL or permissive notices as
recorded above. The audit does not clear Vulkan files, GPL diagnostics,
application plumbing, codecs, or any unreviewed dependency for reuse.
Reimplement any needed component outside this closure independently or audit
its exact license.

## Current gate status

FAAR selects staged-short by default; the legacy single-stage polyphase path remains a fallback for unplanned pairs and a selectable comparison build. Current-hash benchmarks cover 16 common routes among 32, 44.1, 48, 88.2, 96, and 192 kHz. Good and Fast beat the pinned wide SoX CPU path on all 16 routes. High also measured faster on all 16; paired 60-round follow-ups put its 32→48 and 192→48 medians 4.93% and 1.21% below SoX, respectively, with the second result near parity. Selected passband gain probes differ from ARDFTSRC by at most 0.021 dB for High and Good and 0.143 dB for Fast. This confirms the measured passband target on these probes; it is not perceptual certification. Broader perceptual validation and setup-memory reduction for High 192→48 remain open. See the recovery diagnosis, current summary, and 60-round follow-up for scope and raw records.

## SoX CPU versus ARDFTSRC throughput check

A matched 96-to-44.1 kHz check used the same deterministic 30.24-second stereo
float input and an added 35,840-frame zero tail for both programs. SoX used its
CPU-only `rate -v` path from the pinned AMD64 container; ARDFTSRC used quality
16384 and bandwidth 0.99. Across seven alternating runs, ARDFTSRC median
processing CPU was 0.0724 s. SoX median total process CPU was 0.0873 s; its
microsecond-resolution cgroup user CPU was 0.0690 s and system CPU was 0.0189 s.

These measurements are close. ARDFTSRC's timer starts after setup and excludes
WAV I/O. SoX's cgroup timer encloses the whole process, including raw file I/O;
even its user CPU includes that work. SoX's measured user CPU being slightly
lower than ARDFTSRC's processing CPU is evidence in favor of the recollection
that SoX may be faster, but it is not a processing-only comparison and the
difference is within run-to-run spread. The stage plan is more actionable:
SoX selected a 601-tap anti-alias DFT stage followed by a 20-tap polyphase
stage with `L=147`, `M=320`, and 147 phases. That multirate decomposition
avoids applying a 511-tap filter at every output frame. A separate FAAR
implementation should measure an equivalent staged plan against ARDFTSRC and
SoX at matched quality before adoption. Raw runs and provenance are in
[`sox-ardft-cpu-2026-10-18.json`](measurements/sox-ardft-cpu-2026-10-18.json).

## Independent CPU adaptation analysis

The fork's CPU stages use an incremented phase clock. A standalone FAAR
experiment applied the same general idea independently with an exact integer
quotient/remainder recurrence, preserving the existing filter and public API.
On a 30-second stereo 96-to-44.1 kHz High-preset probe, seven GCC `-O3 -flto`
runs measured median CPU time of 1.598 s for the existing loop and 1.605 s for
the recurrence. Both produced the same output checksum and exact frame count.
The change did not improve throughput, so it was not retained. The matrix
fingerprint suite remains green on the unchanged backend. The SoX CPU stage
planner, filter design, and FIR/DFT stages still need independent adaptation
and measurement.

## Ooura FFT license and candidate status

Ooura's official [FFT package page](https://www.kurims.kyoto-u.ac.jp/~ooura/fft.html)
was checked directly. It explicitly permits using, copying, modifying, and
distributing the code for any purpose, including commercial use, without fee;
it requests a package reference when the code is modified. FAAR shipped it
unchanged, with its notice, until the paired FFT in `libfaar/fft_real.c`
replaced it; libfaar no longer contains Ooura code.

The staged-short backend used this FFT when it became the Meson default. Six configured builds pass all 6/6 Meson test groups. On the current workspace library, Good, Fast, and High measured faster than wide SoX on all 16 common routes. A separate 131,072-point FFT experiment used substantially more setup memory without improving CPU time and was rejected. Selected passband gain probes meet the 0.25 dB ARDFTSRC comparison for all three presets. Broader perceptual validation and High 192→48 setup-memory reduction remain open. See the [current diagnosis](faar-recovery-diagnosis.md), [current measurement summary](measurements/faar-recovery-2026-10-18/staged-short-multirate-native/common-rates-current-summary.json), and [60-round paired follow-up](measurements/faar-recovery-2026-10-18/staged-short-multirate-native/high-edge-routes-60trial-summary.json).
