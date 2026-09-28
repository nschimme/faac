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

1. ~~Implement `win` injection~~ -- done this session, see "Step W" below.
2. Implement `sf` injection properly: this needs a callable "quantize at this
   exact scalefactor" primitive, not a post-hoc relabel of `ci->sf[band]`,
   since scalefactor determines the actual quantization step (`qfunc` in
   `assign_band_codebooks`). Reuse `resolve_band_gain`/`qfunc` directly with a
   caller-supplied `sfac` instead of the searched one.
3. Re-run at full scope: 49 clips x 40/48/64 kbps x arms {N13, W, M, C, WM,
   WMC, WMCD} x {ffmpeg, fdk} decode, matching the brief's Tables A-C and
   pre-registered verdict.

## Step W: window-sequence + exact grouping injection (this session)

Implements the `win` field the prior session left undone: donor block_type
(`window_sequence`) and exact short-window grouping (`window_group_length`
per group), so the class/ms match gate stops being clip-luck-dependent on
whatever family FAAC's own transient detector happened to agree with.

### What was built

- FAAD3 (`/private/tmp/claude-501/faac-work/faad3`, `libfaad/decoder.c`
  `core_dump_ics` ~line 264): appended one trailing token to every `C`
  record, `g=<len0>,<len1>,...` (`window_group_length` per group; `g=1` for
  long windows). No existing field touched; parsers split on the fixed
  prefix/`|`, and the new token sits after the last `/`-separated group so
  old parsers ignore it (it never matches the `cb:sf:nnz:ms` token pattern).
  Rebuilt with the same `-Dstats`-style config as before
  (`meson compile -C bs`). Patch: `probe/faad3_g_token.patch`.
- `libfaac/core_inject.h`/`.c`: `CI_WIN` field bit; `CIFrame` now also stores
  the raw `win_seq` and up to 8 `glen[]` group lengths parsed from the new
  `g=` token (via `strstr`, not the per-band tokenizer, so it can't collide
  with a band record). New `CoreInjectLookupWin()` returns the donor's raw
  window layout unconditionally (no window-match gate -- it *is* the
  override that makes later class/ms/sf lookups matchable). `CoreInjectLookup`
  (used by `class`/`ms`) gained a `group_len` parameter: the window-match
  gate now also compares the exact per-group length list, not just group
  *count* (two encoders can agree "3 groups" while splitting the 8 short
  windows differently -- that no longer silently passes).
- `libfaac/frame.c`: moved the `ciFrame`/`ciCh` tagging block from just
  before `AACstereo` to just before `PsyCalculate`/`BlockSwitch`, so both the
  window override and the class/ms/sf lookups key off the same per-frame
  index (removed the old duplicate copy near `AACstereo`; `hEncoder->elements`
  is set up at encoder-open, well before this point, so the move is safe).
- `libfaac/blockswitch.c` `BlockSwitch`: **post-hoc override, not a
  pre-empted desire.** The natural per-channel FSM (`desire` +
  `desired_block_type` hysteresis + `lasttype` legality table) runs
  completely unmodified first. *Then*, for a frame with a donor record
  (looked up once, from the ciCh==0 channel, since all channels already
  share one block-type decision file-wide), the resulting `block_type` is
  swapped for the donor's raw `win_seq` **iff** it's a legal continuation of
  `lasttype` (the same predecessor state the natural decision was just
  judged against); an illegal donor value is silently declined, falling back
  to the natural decision, same as "no donor record". `desired_block_type` is
  overwritten to the *override's* family so the next frame's hysteresis read
  sees the overridden history, not the pre-override natural desire.
  - This design point was not obvious and cost a real debugging pass: an
    earlier version pre-empted `desire` itself (classifying the donor's
    `win_seq` into a short/long "family" and feeding that into the unmodified
    FSM). That version passed on 4/5 self-injection clips but silently
    de-synced on isolated single-frame long/short "blips" (5 frames on the
    German clip). Root cause: the FSM's hysteresis check
    (`desire == SHORT || desired_block_type == SHORT`) reads
    `desired_block_type`, which stores the *raw* `desire` value from the
    previous frame -- not what `block_type` actually came out as when
    hysteresis forced a family switch anyway. A window_sequence value alone
    can't disambiguate "desire was SHORT this frame" from "desire was LONG
    but got hysteresis-forced to the short-side branch", so re-deriving
    `desire` from a decoded `win_seq` trace desyncs exactly at those
    ambiguous blips. Overriding `block_type` post-hoc (after the real,
    unforced FSM already ran, using the real unmodified `desire`) sidesteps
    the ambiguity entirely and fixed all 5 clips to bit-exact self-injection.
- `libfaac/quantize.c` `BlocGroup`: with `win` active and the donor's
  `(frame, ch=0)` record has `win_seq == ONLY_SHORT_WINDOW` and its group
  lengths sum to 8, `coderInfo->groups.n`/`.len[]` are set directly from the
  donor's list, skipping the onset-detector loop entirely (falls back to the
  natural onset detector otherwise -- no donor record, sum mismatch, or
  donor frame isn't really grouped). The per-window zeroing above `cutoff`
  (`w[k] = 0`) still runs unconditionally for every window regardless of
  which path supplies the groups, per the brief.

### Gates

**A -- FIELDS unset, byte-identical:** PASS on all 5 clips x 48 kbps
(`cmp` empty vs the xover-gate N13 `stream.aac` reference for each clip).

**B -- self-injection identity:**
- `FIELDS=win` alone: **PASS, byte-identical on all 5/5 clips**
  (offset 1, the same self-offset established for `class`/`ms` last
  session -- confirms the frame-tagging move didn't change that constant).
- `FIELDS=win,class,ms` self-injected: **4/5 byte-identical**
  (12-German, 15-Good-evening, 21-classic, 35-glockenspiel all
  byte-identical; 24-Greensleeves differs by 1 byte). Bisected by ablation:
  `FIELDS=win,class` alone is byte-identical on Greensleeves too; the
  divergence needs *both* `class` and `ms` together, and **reproduces
  identically with `win` fully OFF** (`FIELDS=class,ms` on this same clip,
  no `win` at all, gives the same 1-byte diff). This proves the divergence
  predates this session's `win` work -- it's a pre-existing `class`+`ms`
  quantizer-retry nondeterminism (decoding both streams shows a handful of
  scalefactors off by 1 in a `class`-forced band's *neighbour*, e.g. frame
  32 band 2: `11:145:9:0` vs `11:144:10:0` -- consistent with the prior
  session's own diagnosis that forcing a band's *class* without also
  forcing its *scalefactor* lets that band's forced decision skip the
  quantizer's normal iterative gain-search, which can very slightly perturb
  neighbouring bands' independently-searched scalefactors in the same
  retry loop). It simply wasn't caught by the prior session's 2-clip gate
  (12, 15 don't happen to trigger it). Not a `win`-injection bug; out of
  scope to fix here (that's exactly what `sf` injection is for).

**C -- fdk donor, `FIELDS=win`, match share:**

| clip | before (natural, no `win`) | after (`win` forced) |
|---|---:|---:|
| 12-German-male-speech | 2/372 (0.5%) | 370/372 (**99.5%**) |
| 15-Good-evening | 44/406 (10.8%) | 398/406 (**98.0%**) |
| 21-classic | 342/456 (75.0%) | 456/456 (**100.0%**) |
| 24-Greensleeves-Korean-male-speech | 6/412 (1.5%) | 410/412 (**99.5%**) |
| 35-glockenspiel | 348/472 (73.7%) | 472/472 (**100.0%**) |

("match" = the encoded stream's own decoded `win_seq` *and* exact group-length
list agree with the donor's, at the correlated frame -- i.e. an exact
per-channel comparison, not just short/long family.) `FAAC_CORE_INJECT_OFFSET=2`
(the fdk-donor constant established last session) confirmed correct: the
window-family signal is too coarse/noisy on its own to re-derive this offset
by sweeping (flat ~21% German-clip "family" match across offsets -2..4), so
this reused the much stronger per-band `class`/`ms` alignment fact instead of
re-deriving it from window data alone. All 5 clips clear the brief's >=98%
bar; German and Greensleeves (previously 0.5-1.5% natural intersection) are
now the biggest movers.

**D -- decode/object-type:** PASS on all 5 `W` streams. FAAD3 `--strict`:
zero concealment, zero non-END termination, all 5. `ffmpeg -xerror`: decodes
clean, all 5. `ffprobe`: `HE-AAC,48000,2` (HE-AAC v1 profile, no PS) on all 5.

### Table: raw MOS, 5 clips x 48 kbps, ffmpeg + fdk decode, ViSQOL python backend

Donor = fdk's native N13-crossover-gate stream (same as last session).
`FAAC_CORE_INJECT_OFFSET=2` for every injected arm.

| clip | arm | bytes | Δbytes | ff MOS | Δff | fdk MOS | Δfdk |
|---|---|---:|---:|---:|---:|---:|---:|
| 12-German-male-speech | N13 | 48162 | -- | 4.0265 | -- | 3.9963 | -- |
| 12-German-male-speech | W | 48436 | +274 | 3.8596 | -0.1669 | 3.7895 | -0.2068 |
| 12-German-male-speech | W+C | 48228 | +66 | 3.9614 | -0.0651 | 3.8980 | -0.0982 |
| 12-German-male-speech | W+M | 48436 | +274 | 3.8662 | -0.1602 | 3.7933 | -0.2029 |
| 12-German-male-speech | W+C+M | 48229 | +67 | 3.9616 | -0.0649 | 3.8976 | -0.0987 |
| 15-Good-evening | N13 | 53056 | -- | 4.0939 | -- | 4.0519 | -- |
| 15-Good-evening | W | 53099 | +43 | 4.0296 | -0.0643 | 3.9888 | -0.0631 |
| 15-Good-evening | W+C | 53229 | +173 | 2.6757 | **-1.4182** | 2.6496 | **-1.4023** |
| 15-Good-evening | W+M | 52998 | -58 | 3.9974 | -0.0965 | 3.9712 | -0.0807 |
| 15-Good-evening | W+C+M | 53145 | +89 | 2.9597 | **-1.1341** | 2.9215 | **-1.1304** |
| 21-classic | N13 | 59284 | -- | 4.1424 | -- | 4.2046 | -- |
| 21-classic | W | 59416 | +132 | 4.1581 | +0.0157 | 4.2129 | +0.0083 |
| 21-classic | W+C | 59396 | +112 | 3.9878 | -0.1547 | 4.0484 | -0.1562 |
| 21-classic | W+M | 59425 | +141 | 4.1313 | -0.0111 | 4.1881 | -0.0164 |
| 21-classic | W+C+M | 59391 | +107 | 4.0320 | -0.1104 | 4.0902 | -0.1144 |
| 24-Greensleeves-Korean-male-speech | N13 | 53685 | -- | 3.8673 | -- | 3.7866 | -- |
| 24-Greensleeves-Korean-male-speech | W | 54128 | +443 | 3.2792 | **-0.5881** | 3.2226 | **-0.5641** |
| 24-Greensleeves-Korean-male-speech | W+C | 53894 | +209 | 3.4127 | -0.4546 | 3.3027 | -0.4840 |
| 24-Greensleeves-Korean-male-speech | W+M | 53933 | +248 | 3.1977 | -0.6696 | 3.1317 | -0.6549 |
| 24-Greensleeves-Korean-male-speech | W+C+M | 53947 | +262 | 3.1321 | -0.7351 | 3.0715 | -0.7151 |
| 35-glockenspiel | N13 | 61421 | -- | 4.5003 | -- | 4.4096 | -- |
| 35-glockenspiel | W | 61692 | +271 | 4.4018 | -0.0985 | 4.2903 | -0.1193 |
| 35-glockenspiel | W+C | 61476 | +55 | 4.3633 | -0.1370 | 4.2903 | -0.1192 |
| 35-glockenspiel | W+M | 61533 | +112 | 4.3741 | -0.1262 | 4.2847 | -0.1248 |
| 35-glockenspiel | W+C+M | 61418 | -3 | 4.3873 | -0.1130 | 4.3105 | -0.0991 |

### Interpretation

**Does the -0.79 (class-only, ~54-67% match) disappear once grouping is
exact (~98-100% match)? No -- it gets substantially worse.** On
15-Good-evening, the prior reduced probe's `class`-only regression at ~58%
match was -0.79/-0.60 MOS (ff, class/class+ms); at ~98% exact-grouping match
this session, `W+C` is **-1.42/-1.13 MOS** (ff, W+C/W+C+M) -- roughly double.
This directly falsifies the hypothesis that the earlier regression was a
window-*misalignment* artifact that exact grouping would clear up. It
confirms the prior session's own alternative diagnosis instead: forcing
fdk's band *class* without also forcing its *scalefactor* is dangerous, and
it gets *more* dangerous as more bands are actually forced (which is exactly
what higher window-match share does -- it doesn't change the class decisions
so much as it multiplies how many of them apply).

**`win` alone is also a net loss, not neutral.** With no class/ms/sf
involved at all, just forcing FAAC to adopt fdk's own block-type/grouping
timing loses on 4/5 clips (up to -0.59 MOS on Greensleeves) and is only
flat-to-slightly-positive on one (classic, +0.01/+0.008). This wasn't
measured directly before (the prior probe's `class`/`ms` arms always rode on
FAAC's *own* natural window choice). The likely mechanism: FAAC's transient
detector and quantizer are co-tuned to each other (this project's own
history includes a blockswitch/TNS co-tune WIN and an SBR transient-detector
retune), so blindly copying another encoder's window-switch timing --
correct for *that* encoder's pre-echo/quantizer behavior -- can be a worse
fit for FAAC's own quantizer than FAAC's own (possibly "wrong" relative to
fdk, but self-consistent) transient call.

**`ms`-only stays roughly neutral to mildly negative** at near-100% window
match (W+M deltas track W closely, e.g. German W=-0.167/W+M=-0.160,
glockenspiel W=-0.099/W+M=-0.126), consistent with last session's ~neutral
finding at lower match share.

**Bottom line for this step:** `win` injection itself is built, gated, and
correct (all 4 gates pass). But it does not unlock a `class`/`ms` win --
if anything it makes the previously-known `class` danger a full order more
visible, and adds its own, independent regression on top. `sf` injection
remains the untested, load-bearing piece the brief's original
quantizer-execution-vs-decision question depends on; forcing `win`+`class`
without it is now confirmed harmful across more than one clip, not a
single-clip fluke.

### Commands / provenance (Step W)

- Worktree: `/private/tmp/claude-501/faac-work/core-inject`, branch
  `core-inject`, HEAD `62f07a81` at session start. Uncommitted; no merge, no
  push. Patch: `git diff -- libfaac/ > probe/core_inject_w.patch`.
- FAAD3 decoder: `/private/tmp/claude-501/faac-work/faad3`, `bs/` build dir
  (`stats=true`). Rebuilt: `CCACHE_DISABLE=1 meson compile -C bs`. Patch:
  `probe/faad3_g_token.patch`.
- Build: `CCACHE_DISABLE=1 meson compile -C build`.
- Corpus: `/Users/nschimme/gitprojects/faac-benchmark/data/external/audio`.
- Self dump: `FAAD_DUMP=<path> FAAD_DUMPTAB=1 <faad3 bs>/frontend/faad
  --strict -q -o /tmp/o.wav <FAAC's own N13 stream>`.
- fdk donor dump: same, reading
  `/private/tmp/claude-501/faac-work/xover-gate/probe/xover_run/<NN_clip>/48/F/stream.aac`
  (fdk's native N-13-crossover stream, as last session; no fdk-aac source
  read, only its pre-generated bitstream from the prior probe's own harness).
- Encode: `FAAC_SBR_START=12 FAAC_SBR_STOP=9 FAAC_SBR_FREQ_SCALE=2
  FAAC_SBR_ALTER=1 FAAC_CORE_INJECT=<dump> FAAC_CORE_INJECT_FIELDS=<win|win,class|win,ms|win,class,ms>
  FAAC_CORE_INJECT_OFFSET=<1 self, 2 fdk-donor> build/frontend/faac
  --overwrite --object-type he-aac-v1 -b 48 -o <out> <clip>.wav`.
- Decode/score: ffmpeg as before; fdk decode via
  `/Users/nschimme/gitprojects/faac-benchmark/bin/fdkdec <in.aac> <out.wav>`;
  score via `NUMBA_DISABLE_JIT=1 <faac-benchmark>/.venv/bin/python
  <faac-benchmark>/scripts/align/sc.py <ref.wav> <wav>`.
- Match-share script (ad hoc, not committed):
  `/tmp/win_match.py`, comparing two FAAD_DUMP files' `C` records
  (`win_seq`, `g=` list) at a caller-supplied frame shift.
- Scratch encodes/dumps/wavs/scores: `probe/scratch_w/` (5 clips: 12, 15, 21,
  24, and 35-glockenspiel; scores.csv holds every arm/decoder MOS + bytes).
