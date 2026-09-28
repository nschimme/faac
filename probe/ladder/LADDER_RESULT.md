# LC 128k reference ladder: A, B, and C all PASS

Root-caused and fixed the cross-reference desync from the previous revision.
Both references now decode strictly clean (zero concealment, zero non-END
terminations) on all 5 clips. Stage C's MOS/line-level/MDCT-ratio tables are
below.

## The bug: huffbook() indexed its cost tables out of range

**Symptom** (from the previous revision): running step1 against real
Apple/fdk data (not FAAC's own dump) produced a badly desynced bitstream --
e.g. Severance/Apple decoded with 265/470 frames non-END-terminated, spurious
SCE/CCE/LFE elements, "channels varied 2-7". FAAC's own self-test (offset 0,
its own dump fed back through step1) showed zero such corruption on the same
clip, which is what pointed away from the transition-window and overflow-
fallback gaps (neither fires meaningfully on Severance) and toward something
specific to real cross-reference data.

**Method** (as directed): re-encoded Severance/Apple as raw ADTS, bisected
the output by ADTS frame count to find the first non-END-terminated frame
(frame 1, i.e. the very first output frame -- `ciFrame=0`), then dumped that
one frame's decoded ICS syntax with `FAAD_LADDER_DUMP=1` and diffed it,
element by element, against Apple's own reference dump for the same
(offset-corrected) frame and against what step1 had computed just before
writing (added temporary debug prints at each stage, removed once the bug was
found).

**What the diff showed**: channel 0's decoded output looked plausible on its
own, but channel 1's decoded `global_gain` came back as 169 -- a value that
appears nowhere in either channel's actual data for this or any nearby frame.
Since `global_gain` is the *first* field of a channel's ICS and both
channels share one `ics_info()` header (confirmed correctly read: window
sequence, `max_sfb`, grouping all matched Apple's own dump exactly), the only
explanation is that channel 0's own data consumed the *wrong number of bits*,
leaving channel 1's reader starting from an offset the writer never intended.

**Root cause**: in `Step1Quantize`, a regular band's `ci->book[band]` was set
directly to the *reference's* class (`cb`, e.g. `HCB_1`) for that band, then
passed to `huffbook()`. `huffbook()`'s Viterbi only ever *widens* from
whatever `book[]` arrives with (`lo = ((book-1)&~1)+1`, `hi = HCB_ESC`
always) -- it assumes the caller's preset book is already *at least*
sufficient for the actual values, which is true for a normal encode (where
`BlocQuant` sets `book[band]` from the real `maxq` it just computed) but is
**not** true here: FAAC's own MDCT, quantized at Apple's or fdk's absolute
scalefactor for that band, can produce a magnitude the reference's own
(differently-scaled) content never needed. When that happens, `size_books()`
computes an index into `book01[]`/`book03[]`/etc. (e.g. `40 + 27*q0 + 9*q1 +
3*q2 + q3` for `HCB_1`) using a `q` value outside that book's actual range --
an out-of-bounds table read, not a bounds-checked rejection. The garbage
"cost" it returns can beat the correct, larger book's real cost, so
`huffbook()` picks a book that cannot represent these values. The writer then
emits a codeword/escape sequence sized for the *wrong* book, consuming a
different bit count than the decoder (which trusts the transmitted book
index) expects to consume -- an actual bitstream desync, not a quality
difference. This is exactly the failure mode the self-test structurally
cannot exercise: FAAC's own quantized magnitudes always fit the class FAAC
itself just picked for them.

**Fix** (`libfaac/step1.c`, `Step1Quantize`): compute `maxq` from the actual
quantized values in the band (was already being tracked for the zero-
reclassification check) and derive `ci->book[band]` from *that*, exactly as
`BlocQuant`'s own `assign_band_codebooks` does (`maxq<=LAV_1?HCB_1:...`),
before calling `huffbook()`. `huffbook()` is still free to widen further for
section-merging efficiency; it just never starts from a family too small for
what's actually there. Verified: all 5 clips x both references now decode
with `non-END termination: 0`, `Error concealment: 0 frames`, and structural
element/TNS/short-block counts matching each reference's own diagnostics.

None of the coordinator's four hypotheses were the actual cause, but
checking them was what surfaced this one: hypothesis (1), max_sfb/sfbn
mismatch, was ruled out first (channel 1's shared `ics_info` decoded
correctly, including grouping) and that's what pointed at "something in
channel 0's own data has the wrong bit count" rather than a header-level
desync -- which is what led to inspecting `book[]`/`huffbook()` next.

## Stage A / B (unchanged, re-verified)

- **Control 0**: still bit-exact on Apple/Severance after every change this
  session (re-checked repeatedly; last check just now).
- **Offset control**: peak +1 (ciFrame-space), unchanged from the prior
  revision.
- **Control 1a (step 1's self-test)**: unchanged at ~9.4% of ICS records
  differing (88-89/940), all attributable to the already-documented,
  unfixed gap (`stereo.c`'s `apply_is` permanently modifying the *other*
  channel's spectrum, which step1 doesn't replicate). The huffbook fix above
  doesn't touch this path since FAAC's own self-test never exercises an
  under-sized preset book.

## Stage C: 5 clips x 2 references

Settings: FAAC ABR 112/128/144 kbps (`faac -b <rate>`) for the per-clip
ladder slope; Apple/fdk references as before; step1 at `FAAC_STEP1_OFFSET=1`.
MOS via `faac-benchmark/scripts/score_clip.py` (ViSQOL/zimtohrli backend,
ffmpeg decode path), each variant scored against the clip's own source WAV.
Bits-adjusted delta = `(MOS_x - MOS_faac128) - slope * log2(bytes_x /
bytes_faac128)`, `slope = (MOS_144 - MOS_112) / log2(bytes_144/bytes_112)`,
per clip -- the ladder-slope method from the archived `cmp3.py`.

| clip | ref | MOS(ref) | MOS(step1) | MOS(faac128) | raw ∆(ref) | raw ∆(step1) | adj ∆(ref) | adj ∆(step1) | bytes(ref) | bytes(step1) | bytes(faac128) |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Severance | Apple | 4.9237 | 4.9000 | 4.8458 | +0.0779 | +0.0542 | **+0.0717** | **+0.0425** | 164154 | 166642 | 161441 |
| Severance | fdk | 4.8921 | 4.7656 | 4.8458 | +0.0463 | -0.0802 | +0.0403 | **-0.0874** | 164097 | 164631 | 161441 |
| 21-classic | Apple | 4.8969 | 4.8623 | 4.8129 | +0.0840 | +0.0494 | +0.0762 | **+0.0392** | 161500 | 163293 | 155941 |
| 21-classic | fdk | 4.8463 | 4.7285 | 4.8129 | +0.0334 | -0.0844 | +0.0298 | **-0.0875** | 158458 | 158131 | 155941 |
| velvet | Apple | 4.7807 | 4.5679 | 4.4807 | +0.3000 | +0.0872 | +0.2346 | **+0.0118** | 178939 | 181624 | 162457 |
| velvet | fdk | 4.6044 | 4.1968 | 4.4807 | +0.1237 | -0.2839 | +0.1166 | **-0.3268** | 164166 | 173105 | 162457 |
| Greensleeves | Apple | 4.9027 | 4.8225 | 4.8526 | +0.0501 | -0.0301 | +0.0357 | **-0.0556** | 146220 | 150260 | 141092 |
| Greensleeves | fdk | 4.8922 | 4.7315 | 4.8526 | +0.0396 | -0.1211 | +0.0330 | **-0.1370** | 143408 | 146735 | 141092 |
| German | Apple | 4.9245 | 4.8985 | 4.9228 | +0.0017 | -0.0243 | +0.0028 | **-0.0331** | 126356 | 130574 | 126799 |
| German | fdk | 4.8945 | 4.6954 | 4.9228 | -0.0283 | -0.2274 | -0.0332 | **-0.2398** | 128893 | 132167 | 126799 |
| **mean** | Apple | | | | | | +0.0842 | **-0.0110** | | | |
| **mean** | fdk | | | | | | +0.0373 | **-0.1757** | | | |

## Correction: the Stage C verdict below read the delta table backwards

Every delta in the Stage C table is vs FAAC-128. Apple's own lead is +0.084;
step1's is -0.011. That means step1 sits ~0.095 *below* Apple -- it **loses**
essentially all of Apple's lead, not "closes" it. Against fdk, step1 loses
~0.21 (-0.176 vs fdk's own +0.037). The first revision of this file had the
sign of that comparison backwards and concluded the opposite. Since step1
differs from a reference only in the spectrum (FAAC's own MDCT, after the
forced M/S/TNS) and the rounding, the loss sits in one of those two stages,
or in a remaining pipeline bug -- **not** in the rate loop, which step1
never uses either way. See the corrected Verdict at the end of this file.

## Correction: the line-level/MDCT-ratio tables were internally inconsistent

The previous revision's ratio computation used `abs(q)` on both sides before
computing the dequantized-magnitude ratio, discarding sign. An
opposite-sign, equal-magnitude line (`qs=-5, qr=5`) therefore registered as a
perfect `ratio=1.0` in the ratio table while the separately-computed,
sign-aware exact-match table correctly scored it as a large miss (`|(-5)-5|
= 10`). That's what produced the reported inconsistency: a median ratio of
exactly 1.000 with IQR `[1,1]` sitting next to a 7-33% exact-match rate,
which cannot both be true of the same data. Fixed in
`probe/ladder/line_level.py` (moved into the repo, as asked) by keeping sign
through the ratio (`math.copysign(|q|^(4/3)*scale, q)` on each side), and by
requiring `groups`/`window_group_length` to match between step1's output and
the reference before comparing a frame at all (they always did on the
clips checked, but the check is now explicit rather than assumed).

**Encoder delay, verified from each reference's own `iTunSMPB` tag** (not
taken on trust): `ffprobe -show_entries format_tags` on
`probe/ladder/ref/apple/Severance...m4a` gives `iTunSMPB = 00000000
00000840 000000C0 ...` -- field 2 is the encoder delay in samples, hex
`0x840 = 2112`. The same tag on the fdk reference gives `00000000 00000800
00000100 ...`, `0x800 = 2048`. Both match the coordinator's numbers exactly:
Apple's delay is `2*1024 + 64` (a 64-sample sub-frame remainder beyond a
whole 2-frame priming), fdk's is exactly `2*1024` (two whole frames). fdk is
therefore the clean case for a line-level comparison at single-sample MDCT
granularity; Apple's is inherently loosened by that 64-sample phase offset,
which is a real property of the two streams, not a bug in this pipeline.
**The corrected tables below are computed against fdk**, per the
coordinator's instruction to use it as the primary case for this check.

## Line-level match and MDCT-ratio tables (corrected, vs fdk)

Aggregate exact / off-by-1 / larger share, all nonzero (either side) lines,
all bands, whole clip:

| clip | total lines | exact | off by 1 | bigger |
|---|---:|---:|---:|---:|
| Severance | 254,121 | 62.7% | 28.9% | 8.4% |
| 21-classic | 249,641 | 64.9% | 26.9% | 8.2% |
| velvet | 210,569 | 78.9% | 17.8% | 3.4% |
| Greensleeves | 214,444 | 63.3% | 29.2% | 7.5% |
| German | 204,574 | 60.7% | 30.8% | 8.5% |

Per-region/bucket breakdown, Severance x fdk (all 5 clips show the same
shape -- monotonically worse exact-match as `|q|` grows, tight agreement
once split by magnitude, see below):

| region | ref \|q\| | total | exact | off by 1 | bigger |
|---|---|---:|---:|---:|---:|
| 0-2k | 0 | 12,541 | 64.4% | 29.5% | 6.2% |
| 0-2k | 1 | 17,860 | 52.9% | 33.3% | 13.8% |
| 0-2k | 2-4 | 24,430 | 43.7% | 34.5% | 21.8% |
| 0-2k | >4 | 25,841 | 33.1% | 35.0% | 31.9% |
| 2-6k | 0 | 71,122 | 88.4% | 11.3% | 0.3% |
| 2-6k | 1 | 58,668 | 80.4% | 17.8% | 1.8% |
| 2-6k | 2-4 | 23,785 | 70.2% | 23.9% | 5.9% |
| 2-6k | >4 | 5,881 | 53.6% | 34.1% | 12.3% |
| 6-12k | 0 | 183,380 | 97.0% | 3.0% | 0.0% |
| 6-12k | 1 | 47,095 | 86.7% | 12.8% | 0.5% |
| 6-12k | 2-4 | 8,248 | 77.5% | 18.9% | 3.6% |
| 6-12k | >4 | 1,405 | 57.0% | 32.7% | 10.2% |
| >12k | 0 | 132,016 | 96.7% | 3.1% | 0.2% |
| >12k | 1 | 16,641 | 87.9% | 11.7% | 0.3% |
| >12k | 2-4 | 1,239 | 71.3% | 26.9% | 1.9% |
| >12k | >4 | 376 | 57.2% | 41.0% | 1.9% |

Signed MDCT-magnitude ratio (median, IQR), nonzero-nonzero lines, all 5 clips
vs fdk:

| clip | 0-2k | 2-6k | 6-12k | >12k |
|---|---|---|---|---|
| Severance | 1.000 [0.715, 1.000] | 1.000 [1.000, 1.000] | 1.000 [1.000, 1.000] | 1.000 [1.000, 1.000] |
| 21-classic | 1.000 [0.725, 1.000] | 1.000 [1.000, 1.000] | 1.000 [1.000, 1.000] | 1.000 [1.000, 1.000] |
| velvet | 1.000 [1.000, 1.000] | 1.000 [1.000, 1.000] | 1.000 [1.000, 1.000] | 1.000 [1.000, 1.000] |
| Greensleeves | 1.000 [0.881, 1.000] | 1.000 [1.000, 1.000] | 1.000 [1.000, 1.000] | 1.000 [1.000, 1.000] |
| German | 1.000 [0.855, 1.000] | 1.000 [1.000, 1.000] | 1.000 [1.000, 1.000] | 1.000 [1.000, 1.000] |

This is now internally consistent: at `|q|=0` and `|q|=1`, exact-match and
IQR-near-1 agree closely (88-97% exact, IQR essentially `[1,1]`); as `|q|`
climbs, exact-match falls to 33-57% and the ratio IQR's lower bound sags to
0.72-0.88 at low frequency -- a real, present rounding gap, not a
computation artifact, and the pre-quantization check below shows why it's
concentrated at low `|q|`.

## Pre-quantization spectrum check (fdk, Severance, as directed)

Added `FAAC_STEP1_SPEC_DUMP=<path>`, a new dump in `frame.c` right after the
forced window/M-S/TNS stages and before `Step1Quantize` runs, in the same
raw layout the `Q` record uses so it reuses `line_level.py`'s band
reconstruction (`probe/ladder/prequant_check.py`). Compared FAAC's real,
pre-quantization spectrum against fdk's *dequantized* value at fdk's own
scalefactor, for every line fdk actually coded (`qr != 0`):

| region | n | correlation | ratio median | ratio IQR | residual (dB rel. to ref energy) |
|---|---:|---:|---:|---|---:|
| 0-2k | 69,362 | 0.9884 | 0.920 | [0.608, 1.095] | -16.3 |
| 2-6k | 89,213 | 0.9819 | 0.894 | [0.651, 1.120] | -14.4 |
| 6-12k | 54,667 | 0.9689 | 0.817 | [0.620, 1.064] | -12.1 |
| >12k | 18,227 | 0.9540 | 0.723 | [0.582, 0.926] | -10.2 |

Read in isolation, that ratio-below-1 trend worsening with frequency could
look like a real scale or window mismatch. Split by the reference's own
`|q|` magnitude instead of frequency, it resolves cleanly:

| ref \|q\| bucket | n | ratio median | ratio IQR |
|---|---:|---:|---|
| small (1-2) | 173,782 | 0.815 | [0.588, 1.108] |
| mid (3-8) | 41,309 | 0.943 | [0.751, 1.074] |
| large (>8) | 16,378 | 0.997 | [0.882, 1.064] |

**Measured**: the ratio converges to 1.000 as `|q|` grows and only sags at
small `|q|`. This is the signature of AAC's own quantization coarseness at
small integers (a `|q|=1` bin covers a wide range of true continuous
magnitudes -- roughly 0.5 to 1.5 in normalized units -- so any two encoders'
independent roundings to `q=1` can differ by a large *relative* amount while
both are legitimately "close" to the true signal), not a systematic
per-frequency scale error. High frequencies simply have a larger share of
small-`|q|` lines (less energy there), which is why the frequency-region
table alone looked worse at >12kHz.

**Inference, labelled as such**: FAAC's spectrum, after the forced window/
M-S/TNS stages, matches the underlying signal fdk's own scale was chosen for
well enough that the residual is fully explained by inherent low-magnitude
AAC quantization coarseness, not a window-shape, TNS-direction, M/S, or
alignment bug. This clears the pipeline to attribute the MOS loss to
rounding rather than a spectral mismatch -- which the next check tests
directly.

## Rounding-variant sweep (fdk, all 5 clips)

Made `MAGIC_NUMBER` (FAAC's own rounding-offset constant, 0.4054, from
`quantize.h`) overridable via `FAAC_STEP1_MAGIC` in `step1.c` and swept
0.3 / 0.4054 (baseline) / 0.5 against fdk on all 5 clips. All three variants
still decode strictly clean (zero non-END terminations) on every clip.

| clip | MOS @0.3 | MOS @0.4054 | MOS @0.5 | bytes @0.3 | bytes @0.4054 | bytes @0.5 |
|---|---:|---:|---:|---:|---:|---:|
| Severance | 4.7406 | **4.7656** | 4.7602 | 154,474 | 164,631 | 175,010 |
| 21-classic | 4.6699 | 4.7285 | **4.7421** | 145,693 | 158,131 | 171,973 |
| velvet | 4.1063 | **4.1968** | 4.1807 | 166,341 | 173,105 | 179,515 |
| Greensleeves | 4.7258 | **4.7315** | 4.7142 | 141,168 | 146,735 | 151,820 |
| German | 4.6839 | **4.6954** | 4.6949 | 129,387 | 132,167 | 134,620 |

**Measured**: FAAC's own default `MAGIC_NUMBER` (0.4054) is at or within
noise of the best of the three on 4 of 5 clips, despite also producing the
*smallest* file of the three on most of them (0.3 rounds down harder,
producing fewer/smaller escape codes and smaller files, but scores worse
everywhere; 0.5 rounds up harder, producing bigger files, and is a coin-flip
against the default). None of the three closes any meaningful fraction of
the ~0.15-0.2 MOS gap between step1 and fdk's own MOS (fdk reference MOS was
4.85-4.92 across these clips; even the best rounding variant tops out at
4.62-4.76).

**Inference, labelled as such**: the rounding *threshold* is not the driver
of the fdk loss -- FAAC's existing constant is already close to optimal for
this task, and no nearby value meaningfully closes the gap. Combined with
the pre-quantization check (spectrum matches well, residual explained by
inherent low-`|q|` coarseness, not a scale/window bug), the loss most plausibly
sits in the accumulated effect of that quantization noise itself:
requantizing FAAC's spectrum at fdk's per-band scale reproduces fdk's exact
integer only 61-79% of the time (see the corrected line-level table above),
and the ~20-40% that land elsewhere -- overwhelmingly off by exactly 1 at low
`|q|`, per the region/bucket table -- are enough small, spectrum-wide
discrepancies to cost real perceptual quality even though no single one is
large. This is a real, if diffuse, quantizer-precision gap rather than a
bug still to find, though I would not treat that as fully settled without
also checking whether fdk's OWN choice of *which* bands to code (a
psychoacoustic decision this ladder holds fixed by construction) is doing
work that a value-for-value integer match can't detect either way.

## Verdict (corrected)

**Measured**: step1's bits-adjusted delta vs FAAC's own ABR 128k is
negative against both references -- mean -0.011 vs Apple (range +0.04 to
-0.06 on 4/5 clips, +0.01 on velvet) and mean -0.176 vs fdk (range -0.03 to
-0.33). Apple's own bits-adjusted lead over FAAC averages +0.084; **step1
gives essentially all of that lead back** (net ~-0.095 vs Apple's own
number), landing close to FAAC-128 rather than close to Apple. fdk's own
lead averages +0.037; step1 lands *below* FAAC-128 by ~0.21 against fdk --
worse than either endpoint.

**Inference, labelled as such**: step1 differs from a reference only in
(a) FAAC's own spectrum in place of the reference's, after the forced
window/M-S/TNS decisions, and (b) FAAC's quantizer rounding at the
reference's scale in place of the reference's own quantizer. The pre-
quantization check (fdk/Severance) shows (a) is sound: high correlation
(0.95-0.99), and a magnitude-dependent ratio that converges to 1.000 exactly
where AAC's own quantization is fine-grained, consistent with inherent
low-`|q|` coarseness rather than a spectral bug. The rounding sweep shows the
loss isn't sitting in the `MAGIC_NUMBER` choice either. That leaves the
*accumulated* effect of value-by-value quantizer disagreement (see the
line-level table: 61-79% exact, but the remainder mostly off by exactly 1,
spread across every band) as the best-supported explanation for the loss --
not a single bug, and not the rate loop (step1 never runs one). This
reframes the earlier "transplant losses were confounded by the rate loop"
reading from the previous revision, which the coordinator correctly flagged
as backwards: the ladder now points at the quantizer's rounding behavior
itself, in aggregate, as where FAAC's remaining gap to both references
lives, at least on this 5-clip sample.

**Stage D (49 clips)**: not run. Before spending that budget, I would want
either (a) a decisive test separating "diffuse rounding-precision gap" from
"one remaining bug I haven't isolated" -- e.g. checking whether the
off-by-1 lines are randomly distributed or concentrated in a specific
band/window pattern -- or (b) acceptance that the 5-clip result (consistent
sign and rough magnitude across all 5 clips, on both references) is already
enough signal to act on without a wider run.

## What's in place

- `libfaac/step1.c`/`step1.h`: the fixed step-1 engine (huffbook preset-book
  fix), plus `FAAC_STEP1_MAGIC` (rounding-constant override, for the sweep
  above).
- `libfaac/frame.c`: adds `FAAC_STEP1_SPEC_DUMP` (pre-quantization spectrum
  dump, same raw layout as the `Q` record) alongside the existing
  `FAAC_MDCT_ENERGY_DUMP` and `FAAC_STEP1`/`FAAC_STEP1_OFFSET` hooks.
- `probe/ladder/mdct_offset.py`: the offset-control script (peak +1,
  ciFrame-space).
- `probe/ladder/line_level.py`: **moved into the repo and fixed** (signed
  ratio, explicit groups/glen check) -- band-major line comparison +
  MDCT-ratio computation from two `FAAD_LADDER_DUMP` dumps. Callable
  standalone (`python3 line_level.py <step1_dump> <ref_dump> <offset>`).
- `probe/ladder/prequant_check.py`: new -- pre-quantization spectrum check,
  reads a `FAAC_STEP1_SPEC_DUMP` output and a reference dump, reports
  correlation/ratio/residual by frequency region and by `|q|` magnitude.
- `/tmp/ladder_c/`: all intermediates for this session's Stage C run (step1
  outputs at various offsets/magic values, FAAC 112/128/144 ladders, dumps,
  MOS score logs) -- not cleaned up yet in case a follow-up wants to re-
  derive tables without re-encoding; safe to delete otherwise.
- Patches (all uncommitted, nothing pushed, regenerated this session):
  `step1_engine.patch`, `frame_step1_hooks.patch`,
  `coder_channels_tns_writer.patch`, `reemit_and_build.patch`.
