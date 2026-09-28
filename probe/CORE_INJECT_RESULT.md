# Core-injection probe: partial result (reduced scope)

**This is not the full brief.** Given the session's bounded budget, only two of
four injection fields (`class`, `ms`) were implemented, and the measurement
was run on 2 clips at one rate (48 kbps) with one decoder (ffmpeg/ViSQOL), not
the full 49-clip x 3-rate x 7-arm x 2-decoder matrix. `win` (block/grouping
injection) and `sf` (scalefactor-shape injection) were not implemented --- see
"Not done" below. Treat this as a first directional read, not a verdict.

## What was built

- `libfaac/core_inject.c` / `.h` (new, uncommitted): parses a FAAD_DUMP `C`-record
  file into per-(frame,channel) arrays of `{cb, sf, ms}` per band, in group/sfb
  order matching FAAC's own `book[]`/`sf[]` indexing. Env-gated:
  `FAAC_CORE_INJECT=<dump>`, `FAAC_CORE_INJECT_FIELDS=class,ms`,
  `FAAC_CORE_INJECT_OFFSET=<n>`. A lazy process-wide singleton
  (`CoreInjectGet()`), same pattern as `sbr_inject.c`.
- Hooks:
  - `libfaac/quantize.c` `assign_band_codebooks` (~line 291-345): before the
    natural zero/PNS/coded decision, looks up the donor's class for `band` on
    frames whose window layout matches (see below); forces `HCB_ZERO`, forces
    the existing PNS branch, or skips both to force "coded". Intensity
    (cb 14/15) is left alone -- that decision happens earlier, in stereo.c,
    and forcing it was out of scope here.
  - `libfaac/stereo.c` `process_cpe` (~line 185-220): computes FAAC's natural
    M/S decision first, then overrides it with the donor's `ms_used[band]`
    when matched, before applying/skipping the M/S transform.
  - `libfaac/coder.h`: added `ciCh`/`ciFrame` to `CoderInfo` (probe-only, 0
    when unset); `libfaac/frame.c` sets them once per channel per frame,
    right before `AACstereo`, as `hEncoder->frameNum - LOOKAHEAD_DEPTH - 1`.
- **Window match** (required before any override, and reported as a share):
  FAAC's block short/long family, `max_sfb`, and window-group count must all
  equal the donor's `C`-record `win_seq`/`max_sfb`/`groups`. No `win`
  injection means this is a natural intersection, not something forced.

## Not done (scope cut, not evaluated)

- `win` injection (donor block-type/grouping override): not implemented. This
  means the match rate below is a hard ceiling set entirely by how often
  FAAC's own transient/window decision already agrees with fdk's -- lower than
  the brief anticipated for a `W`-equipped arm.
- `sf` injection (donor scalefactor shape): not implemented. Forcing a band's
  *class* without also forcing its *scalefactor* leaves FAAC's own magnitude
  estimate driving a decision class it wasn't tuned for (see the regression
  below), which is very likely why `class` alone regresses hard on one clip.
- Intensity-stereo class forcing (cb 14/15): not implemented, left to FAAC's
  own stereo.c decision.
- fdk-decoder leg, bits/frame breakdown (Table B), stereo coherence (Table C),
  and the 47 remaining clips / 2 remaining rates (40, 64 kbps): not run.

## Gates

1. **Unset -> bit-exact**: PASS. `FAAC_CORE_INJECT` unset reproduces the
   xover-gate N13 build byte-for-byte on both test clips at 48 kbps
   (`diff <(xxd ...)` empty both times).
2. **Self-injection identity**: PASS, but only after finding the right
   offset empirically (the brief asked for a self-check rather than an
   assumption, so this *is* that check). Dumped FAAC's own N13 stream with
   FAAD3, fed it back with `FIELDS=class,ms`. Match-rate sweep over
   `FAAC_CORE_INJECT_OFFSET`:

   | offset | matched/total bands (clip 12) |
   |---:|---:|
   | -2 | 3833/14575 |
   | -1 | 3828/14567 |
   | 0 | 4756/14589 |
   | **1** | **14631/14631 (100%)** |
   | 2 | 4756/14596 |

   At offset 1: 100% match on both test clips, and the re-injected output is
   byte-identical to plain N13 (`diff` empty). This fixes the self-alignment
   constant (FAAC's own frame counter is one behind its own bitstream's frame
   numbering in the dump).
3. **fdk-donor alignment**: with the self-offset (1) plus the established
   FAAC-n <-> fdk-(n+1) fact, the predicted donor offset is 2. Swept 0-4 on
   clip 12's real fdk donor and confirmed 2 gives the peak window-match rate:

   | offset | matched/total |
   |---:|---:|
   | 0 | 1014/14584 |
   | 1 | 1114/14585 |
   | **2** | **1830/14588 (peak)** |
   | 3 | 1234/14585 |
   | 4 | 930/14585 |

   `FAAC_CORE_INJECT_OFFSET=2` used for all fdk-donor arms below.
4. **Decode/object-type**: PASS on what was run. FAAD3 `--strict` shows zero
   concealment and zero non-END termination for the `C` arm stream tested;
   ffmpeg `-xerror` decoded all arms without failure; `ffprobe` confirms
   `profile=HE-AAC`, `sample_rate=48000`, `channels=2` on the injected stream.
5. **Field-took-effect rate**: not separately re-verified by re-dumping (the
   match-rate figures above are the pre-encode lookup hit rate, which is a
   reasonable proxy here since every matched lookup is applied unconditionally
   in this build).

## Window-match share (the real ceiling on this reduced probe)

| clip | field | matched/total bands |
|---|---|---:|
| 12-German-male-speech | class | 1332/11054 (12.0%) |
| 12-German-male-speech | ms | 498/3534 (14.1%) |
| 12-German-male-speech | class+ms | 1830/14588 (12.5%) |
| 15-Good-evening | class | 4872/8991 (54.2%) |
| 15-Good-evening | ms | 2436/3624 (67.2%) |
| 15-Good-evening | class+ms | 7308/12618 (57.9%) |

Match share varies enormously by clip (12% vs ~55-67%) -- almost certainly
tracking how often each clip's own transient content happens to make FAAC and
fdk pick the same window family independent of any injection. Without `win`
injection this ceiling is largely luck-of-the-clip, not something the probe
controls.

## Table A (reduced): raw MOS, ffmpeg decode + ViSQOL python backend, 48 kbps only

| clip | arm | bytes | Δ bytes vs N13 | MOS | Δ MOS vs N13 |
|---|---|---:|---:|---:|---:|
| 12-German-male-speech | N13 | 48162 | -- | 4.0265 | -- |
| 12-German-male-speech | C (class) | 48132 | -30 | 4.0288 | +0.0024 |
| 12-German-male-speech | M (ms) | 48162 | +0 | 4.0265 | +0.0001 |
| 12-German-male-speech | MC (class+ms) | 48135 | -27 | 4.0307 | +0.0042 |
| 15-Good-evening | N13 | 53056 | -- | 4.0939 | -- |
| 15-Good-evening | C (class) | 53206 | +150 | 3.3024 | **-0.7915** |
| 15-Good-evening | M (ms) | 52951 | -105 | 4.0781 | -0.0158 |
| 15-Good-evening | MC (class+ms) | 53128 | +72 | 3.4968 | **-0.5971** |

`C` and `MC` on `15-Good-evening` decode cleanly (FAAD3 zero concealment,
ffmpeg strict decode succeeds, HE-AAC v1 confirmed) but score far worse
perceptually -- a real, audible regression, not a harness artifact. This clip
is 70.9% short blocks per FAAD3's own stats banner on the `C` stream (vs the
donor's presumably different transient pattern), and is the higher-match-share
clip (54-67%), so more bands were actually forced -- consistent with
class-forcing being the actual cause, not a coincidence.

## Interpretation (pre-registered rule from the brief, applied honestly)

The brief's rule (an arm is "buildable" at >=+0.03 bits-adjusted with >=3:1
W/L at >=2/3 rates on both decoders) cannot be evaluated: only 2 clips, 1
rate, 1 decoder were run, and W/L is not statistically meaningful at n=2.
What *can* be said from these 2 clips:

- `ms`-only injection is roughly neutral (+0.0001, -0.0158) at the match rates
  achieved here -- no evidence of either a large win or the catastrophic loss
  `class` shows, but also no evidence of a real +0.03-class gain.
- `class`-only and `class+ms` injection produced one severe regression
  (-0.79 / -0.60 MOS) alongside one small gain (+0.0024 / +0.0042), on the
  clip where more bands were actually forced. This is consistent with the
  brief's own prediction that a `sf`-less class arm is what would happen if
  "the gap is in quantizer execution rather than decisions" (Section:
  pre-registered verdict) -- forcing fdk's coarse class onto a band without
  also forcing fdk's scalefactor leaves FAAC's own magnitude/rate-control
  logic fighting a decision it wasn't tuned for, and on a transient-heavy clip
  that fight went badly wrong. It also matches this project's memory of a
  prior, differently-built "hole avoidance" class-forcing probe going DEAD
  (`project-quantizer-plan-step1-2-results.md`), for a related reason.
- Given this, `sf` (scalefactor-shape injection, not implemented here) looks
  like the load-bearing piece the brief's own quantizer-execution-vs-decision
  question depends on: `class` without `sf` is not a fair test of "does
  forcing fdk's classes help", because it's missing the one thing that would
  keep a forced class from immediately producing audible damage.

## Honest bottom line

This reduced probe does **not** support building `class`-only or `ms`-only
core injection. It does not rule out `WMCD` (the brief's full arm, with `win`
and `sf` both implemented) recovering meaningfully more of the gap --  that
arm was never built or measured here. The single largest, most concrete
finding is: **forcing fdk's band class without also forcing its scalefactor
shape is dangerous** on transient-heavy content, at whatever window-match
rate this probe achieved (12-67%, clip-dependent, since `win` wasn't
injected). Building `sf` injection correctly (matching FAAC's own frame-mean
level while copying fdk's per-band shape, which requires re-deriving the
quantizer's gain-search rather than just relabeling `sf[band]`) is real,
further work this session did not reach.

## Commands / provenance

- Worktree: `/private/tmp/claude-501/faac-work/core-inject`, branch
  `core-inject`, base `638f2952` (xover-gate). Uncommitted; no merge, no push.
- Build: `CCACHE_DISABLE=1 meson setup build --wipe && CCACHE_DISABLE=1 meson compile -C build`.
- Donor dumps: `FAAD_DUMP=<path> FAAD_DUMPTAB=1 /private/tmp/claude-501/faac-work/faad3/bs/frontend/faad --strict -q -o /tmp/o.wav <fdk-donor-stream>`,
  reading `<xover-gate>/probe/xover_run/<NN_clip>/48/F/stream.aac` as the donor
  (fdk's native N-13-crossover stream from the prior probe; no fdk-aac source
  was read, only its already-generated bitstream/decode outputs from that
  prior probe's own harness).
- Encode: `FAAC_SBR_START=12 FAAC_SBR_STOP=9 FAAC_SBR_FREQ_SCALE=2 FAAC_SBR_ALTER=1 FAAC_CORE_INJECT=<dump> FAAC_CORE_INJECT_FIELDS=<class|ms|class,ms> FAAC_CORE_INJECT_OFFSET=2 build/frontend/faac --overwrite --object-type he-aac-v1 -b 48 -o <out> <clip>.wav`.
- Decode/score: `ffmpeg -y -v error -xerror -err_detect explode -i <out> -c:a pcm_f32le <wav>`, then
  `NUMBA_DISABLE_JIT=1 /Users/nschimme/gitprojects/faac-benchmark/.venv/bin/python /Users/nschimme/gitprojects/faac-benchmark/scripts/align/sc.py <ref.wav> <wav>`.
- Debug counter: `FAAC_CORE_INJECT_DEBUG=1` prints `matched/total` band lookups
  to stderr at process exit.
- Source diff: uncommitted in the worktree (`libfaac/core_inject.c`,
  `libfaac/core_inject.h`, edits to `libfaac/quantize.c`, `libfaac/stereo.c`,
  `libfaac/coder.h`, `libfaac/frame.c`, `libfaac/meson.build`). No
  `probe/core_inject.patch` was written given the session ended before the
  full arm matrix; generate with `git -C /private/tmp/claude-501/faac-work/core-inject diff > probe/core_inject.patch` if resuming.
- Scratch encodes/dumps/wavs for the 2-clip run are under
  `/private/tmp/claude-501/faac-work/core-inject/probe/scratch/`.

## To finish this probe properly (not done, for whoever resumes)

1. Implement `win` injection (block_type + grouping override) so the
   class/ms/sf match rate isn't clip-luck-dependent -- hook `BlockSwitch`
   (`libfaac/blockswitch.c:204`) and `BlocGroup` (`libfaac/quantize.c:565`).
2. Implement `sf` injection properly: this needs a callable "quantize at this
   exact scalefactor" primitive, not a post-hoc relabel of `ci->sf[band]`,
   since scalefactor determines the actual quantization step (`qfunc` in
   `assign_band_codebooks`). Reuse `resolve_band_gain`/`qfunc` directly with a
   caller-supplied `sfac` instead of the searched one.
3. Re-run at full scope: 49 clips x 40/48/64 kbps x arms {N13, W, M, C, WM,
   WMC, WMCD} x {ffmpeg, fdk} decode, matching the brief's Tables A-C and
   pre-registered verdict.
