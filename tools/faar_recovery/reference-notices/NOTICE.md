# Reference code and dependencies

This directory supports local diagnostics. None of these reference drivers or
external libraries are linked into `libfaar` or installed by its build.

- `ardftsrc_reference.cpp` is the existing instrumented reference from
  `faac-benchmark/scripts/ardftsrc_bench.cpp`, SHA-256
  `a342a9a2dcc36035399c174b958fed3af4bd16a1979ac87838b050121b6ebb8b`.
  Its numerical implementation is from mrspoonsi/ARDFTSRC revision
  `93d5af55a3fa7442b57a56cb956b15c841173f7f`. Its original WTFPL header,
  Mycroft/dBpoweramp attribution, and repository MIT license are retained;
  see `ARDFTSRC-LICENSE`. Recovery instrumentation only brackets the existing
  processing timer with Callgrind client requests in a scratch copy.
- AudioFile is external, MIT, adamstark/AudioFile revision
  `4554279566c19602a14868b58551f23fe5c5fb53`. Eigen is external, MPL-2.0.
- The diagnostic ARDFTSRC and FFT benchmark executables link external FFTW,
  GPL-2.0-or-later. This is a reference dependency, not cleared for FAAR reuse.
- KBSRC is external, MIT, revision
  `9bb02260d795350b12ac6632fed40343bffd7b86`. The scratch copy retains its
  license. Only its clock is changed to process CPU time and optional
  Callgrind collection is added; arithmetic and dispatch remain unchanged.
- SoX Vulkan CPU reference is external, revision
  `54102e40ffbdbe991505b9ac0a7c62e0b4d4ae3c`. Its full reference library
  includes GPL diagnostics. Scratch copies retain original notices and licenses.
  Recovery additions export numeric coefficients and increase debug-print
  precision; the numerical pipeline is unchanged. The separate SoX file audit
  in `docs/faar-sox-rate-audit.md` controls any future implementation reuse.

The Decimal oracle, test runner, reference bindings, and recovery scripts are
independently written local tooling. Tuneout-derived tests continue to retain
their Apache-2.0 license and attribution under `libfaar/verification`.
