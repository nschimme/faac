# Project handoff: faac speech-quality work on `he-aac-v2`

Snapshot date: **2026-04-27**. Branch: `he-aac-v2` ahead of `master`
on local commits — **not pushed**. Working tree clean except untracked
`summary_raw_v3.txt` (stale from earlier session, safe to delete).

---

## Branch state — recent commit chain

```
9ea3ce3  frontend, frame: stop HE-AAC v1 catastrophe on narrow-band inputs   (2026-04-27)
fc78245  quantize/frame: lower default PNS level and raise SFB-kill floor    (2026-04-26)
673dc8a  Revert "quantize: retune bmask masking constants for narrow-band"   (2026-04-26)
cf6e965  [REVERTED] bmask retune that was bit-overspend, not coding gain
5417440  quantize: floor energy ratio in bmask so quiet bands don't collapse
44a1d0f  auto-mode: narrow HE-AAC crossover to [20, 32] kbps/ch
b67a38a  sbr_tables: attribute FFmpeg/FAAD2 sources for reproduced data
1583404  sbr: restrict QMF modulation to [kx,k2); fuse polyphase
3b7df76  sbr: portable perf in 64-band QMF kernel (MOS-neutral)
```

Read `git log --grep="Co-Authored-By: Claude"` to see the full
process trail with measurements.

---

## What's shipped on this branch

### `fc78245` — LC-core retune (the speech lever)

Two LC-core constants:
- `frame.c:422`  `pnslevel` default **4 → 2**
- `quantize.c:98` `NOISEFLOOR` **0.4 → 2.0**

Mean ViSQOL speech-mode MOS on 12-clip stratified voip subset:

| | HEAD (pre fc78245) | post fc78245 |
|---|---|---|
| voip 12-clip (faac LC -b 16) | 3.051 | **3.216** (+0.165) |
| he64 9-clip (auto -b 64) | 3.082 | 3.082 (±0) |
| lc128 4-clip (LC -b 128) | 4.349 | **4.395** (+0.046) |

All matched-bps (output bytes ≤±2% drift; this is coding gain, not
bit overspend). Music gates clean. Ratio of gap closed to libaacplus
on voip: ~24% (0.686 → 0.521).

### `9ea3ce3` — HE-AAC v1 narrow-input fix

Two-leg change:

1. **Library**: `libfaac/frame.c:276-281` auto-mode gate also checks
   `hEncoder->sampleRate >= 32000` so HE is never silently picked on
   inputs that would yield core SR < 16 kHz.
2. **Frontend**: new `frontend/upsample.{c,h}` (31-tap halfband FIR,
   ~50 LOC each, original public-DSP code) plus wiring in
   `frontend/main.c` to 2× upsample explicit `--object-type he-aac`
   on inputs in [16, 32) kHz. Inputs < 16 kHz refused with a clear
   stderr message.

Verification (12-clip voip):

| invocation | MOS (pre) | MOS (post) |
|---|---|---|
| `--object-type he-aac -b 16` (16 kHz mono) | 2.145 | **3.154** (+1.009) |
| `--object-type lc -b 16` | 3.216 | 3.216 (unchanged) |
| 9-clip he64 music auto | 3.082 | 3.082 (unchanged) |
| 4-clip lc128 music | 4.395 | 4.395 (unchanged) |
| `--object-type he-aac` on 8 kHz input | catastrophic | refused with stderr |

HE on speech now ties LC at matched bps. Still trails libaacplus
(~0.58 below); closing that requires deeper psymodel/bitalloc work
that is **out of scope** for this branch.

### `673dc8a` — revert of `cf6e965`

`cf6e965` looked like a +0.145 voip win but post-ship audit (after
adding bps tracking to the harness) found it was spending +1,488 bps
more than parent on voip — pure bit overspend, no coding gain. At
matched bps `cf6e965` actually *lost* 0.045 to its parent. Reverted.
**Lesson recorded**: always track per-axis output bps in sweeps.

---

## Repos and paths

All on the WSL2 home directory of the original PC.

| dir | purpose | notes |
|---|---|---|
| `/home/yoyososo/faac` | this repo | branch `he-aac-v2`, ahead of `master` |
| `/home/yoyososo/faac-benchmark/data/external/speech` | 12-clip voip + 12-clip vss subsets | used by all sweep harnesses |
| `/home/yoyososo/faac-benchmark/data/external/audio` | music corpus | 9-clip he64 + 4-clip lc128 gates |
| `/home/yoyososo/libaacplus` | reference encoder | binary at `frontend/aacplusenc`, **commercial-licensed source — analyze only, never copy** |
| `/home/yoyososo/faad2` | decoder | local CMake build at `build_inst/` is patched with FAAD_DUMP env-gated SBR/scalefactor dump |
| `/home/yoyososo/visqol` | ViSQOL binary + speech-mode lattice TFLite | speech model: `model/lattice_tcditugenmeetpackhref_ls2_nl60_lr12_bs2048_learn.005_ep2400_train1_7_raw.tflite` |
| `/home/yoyososo/.claude/projects/-home-yoyososo-faac/memory/` | project memories (Claude Code persistent) | copy alongside repo for full context |
| `/home/yoyososo/.claude/plans/silly-tickling-neumann.md` | active plan (HE fix marked SHIPPED) | copy alongside |

`/tmp/lc_diag/` holds harness scripts and per-cell encode outputs.
**Volatile across reboots; recreate from below.**

---

## Diagnostic infrastructure to recreate

### 1. Build deps on the new PC

```bash
sudo apt install meson ninja-build cmake gcc g++ pkg-config
sudo apt install ffmpeg faad libsndfile1-dev
# ViSQOL: build from source (see visqol repo); use the speech-mode
# lattice TFLite model.
```

faac build:
```bash
cd /home/yoyososo/faac
meson setup build_rel --buildtype=release
meson compile -C build_rel
# binary: build_rel/frontend/faac
```

faad2 instrumented build (decoder dump):
```bash
cd /home/yoyososo/faad2
cmake -B build_inst -DCMAKE_BUILD_TYPE=Release
cmake --build build_inst -j4
# binary: build_inst/faad
```

The instrumentation in faad2 lives in `libfaad/syntax.c` —
`faad_dump_lc()` emits one CSV line per channel per frame to stderr
when `FAAD_DUMP=1` is set in the environment. This is a local
analysis tool, not redistributable.

### 2. Sweep harness `lever_sweep.sh`

The canonical bps-tracked harness. Lives at
`/tmp/lc_diag/lever_sweep.sh` on the original PC. Pattern:
parallel xargs over per-axis clip lists, emits one summary line:

```
TAG  voip=X.XXX he64=X.XXX lc128=X.XXX  dV=±X.XXX dH=±X.XXX dL=±X.XXX  bps[v=N(+P%) h=N(+P%) c=N(+P%)]
```

Baselines hardcoded in the awk at the bottom. Current values:
- voip 3.051, he64 3.082, lc128 4.349 (= post-revert HEAD pre fc78245)

After fc78245+9ea3ce3 ship, the equivalent run produces voip 3.216,
he64 3.082, lc128 4.395.

The 12 voip clip names, 9 he64 music clip names, and 4 lc128 music
clip names are inlined in the harness — they're stratified subsets
(echo, noise, chop, clip, compspkr × FA/FG/MK/ML voice strata).
See harness body for the lists.

Reusable helper to write on the new PC:

```bash
# Skeleton only — fill in clip lists from the original or from
# project_speech_lc_core.md
mkdir -p /tmp/lc_diag/sweep
# (transcribe the full lever_sweep.sh from the original PC; or
#  rebuild from the patterns in this file's commit messages)
```

### 3. Bitstream forensics harness

Pattern: encode same clip with both faac LC and libaacplus, decode
both with the instrumented faad2, then aggregate per-frame stats.

Aggregate stats from 12-clip voip on 2026-04-26:

| metric | faac LC -b 16 | libaacplus -b 16 |
|---|---|---|
| TNS active | 0% | 35% |
| PNS active | 41% | 0% |
| SHORT-block | 18% | 3% |
| codebook 0 (silence) sections | 6 | 62 |
| mean nonzero scale factor | 112 | 154 |

These numbers drove the fc78245 axis selection (PNS down, SFB-kill
up). TNS-on alone gave +0.076 but did not stack on the PNS+NOISEFLOOR
baseline; left dropped.

### 4. libaacplus invocation note

libaacplus refuses input that would yield core AAC SR less than half
the input rate. Speech voip clips at 16 kHz mono need to be
upsampled to 32 kHz mono before feeding to `aacplusenc`:

```bash
ffmpeg -y -i input16k.wav -ar 32000 -ac 1 input32k.wav
/home/yoyososo/libaacplus/frontend/aacplusenc input32k.wav out.aac 16000 m
```

For vss 40 kbps libaacplus needs 48 kHz stereo input.

---

## License firewall (very important)

libaacplus is under a **commercial license**. Recorded in
`project_libaacplus_license_firewall.md`.

- **Allowed**: instrumenting libaacplus binaries locally with printfs
  to extract per-frame internal state for analysis. Reading its
  decoded bitstream output. Using observed numerical patterns as a
  behavioral specification.
- **Allowed**: reading public references — ISO/IEC 14496-3, AAC
  papers (Brandenburg, Bosi, Quackenbush) — and implementing those
  algorithms freshly in faac.
- **NOT allowed**: copying or paraphrasing libaacplus source into
  faac, even with renamed symbols, even with adapter layers.
- Commit messages must explicitly state "no libaacplus source
  incorporated." Both fc78245 and 9ea3ce3 do.

---

## Memory files to transfer

Located at
`/home/yoyososo/.claude/projects/-home-yoyososo-faac/memory/`. The
critical ones for this work:

- `MEMORY.md` — index
- `project_speech_lc_core.md` — full diagnostic + retune history,
  ends with "HE-narrow-input fix SHIPPED (9ea3ce3)"
- `project_libaacplus_license_firewall.md` — the firewall rule
- `feedback_sweep_track_bps.md` — the lesson from cf6e965
- `project_lc_core_bottleneck.md` — earlier music-LC-core work
- `heaac_bitrate_baseline.md` — auto-mode crossover history
- `project_sbr_voice_regression_closed.md` — why the [20, 32] gate
  exists; do not re-widen blindly

Also: `/home/yoyososo/.claude/plans/silly-tickling-neumann.md` —
the active plan, marked SHIPPED.

Just `tar cz` the whole `~/.claude/projects/-home-yoyososo-faac/`
directory plus the `~/.claude/plans/` directory and copy them over.

---

## Open follow-ups (none blocking)

1. **Push the branch.** The two commits on `he-aac-v2` are
   local only. The maintainer should push when ready and may want
   the full 947-case CI run to confirm the LC and music gates hold
   broadly. Local 12+9+4-clip gates are clean; CI is the wider check.
2. **Remaining ~0.52 MOS gap to libaacplus on voip.** Sits in
   deeper psymodel/bitalloc territory: per-SFB allocation patterns,
   scale-factor refinement granularity, masking-spread shape. The
   bitstream forensics from this session (PNS/TNS/SHORT/cb0
   distributions) are documented in `project_speech_lc_core.md`
   and would be the starting point for a future plan. Estimated
   scope: 1-2 weeks; firewall must be enforced strictly.
3. **vss 40 kbps speech**. The auto-mode crossover narrowing
   protects vss too, but the LC-core retune (fc78245) only sees
   gates at -b 16 / -b 64 / -b 128. vss 40 kbps was not in the
   gate. Worth a one-shot measurement before claiming the LC retune
   is universally clean.
4. **Stretch: faac HE-AAC v1 actually beating faac LC on speech.**
   Currently HE ties LC after 9ea3ce3 (both ~3.15-3.22 on 12-clip
   voip). Beating LC would require structural SBR retuning toward
   the libaacplus master grid (kx=28/k2=49/4-LOW/2-3-env vs faac's
   kx=31/k2=62/30-HIGH/1-env per
   `project_sbr_master_grid_finding.md`). That work was scoped out
   of the 9ea3ce3 plan.

---

## Quick-start on the new PC

```bash
# 1. Clone faac at this branch's HEAD.
cd ~ && git clone <faac-remote> faac
cd faac && git checkout he-aac-v2
# 2. Restore commits if not yet pushed:
#    cherry-pick or fetch the local 9ea3ce3 / fc78245 patches.

# 3. Build.
meson setup build_rel --buildtype=release
meson compile -C build_rel

# 4. Build instrumented faad2 if you need bitstream dumps:
git clone <faad2-remote> ~/faad2 && cd ~/faad2
# Re-apply the FAAD_DUMP printf hook in libfaad/syntax.c
# (~50 LOC; see syntax.c history on the original PC, or grep for
#  "FAAD_DUMP" / "faad_dump_lc")
cmake -B build_inst -DCMAKE_BUILD_TYPE=Release
cmake --build build_inst -j4

# 5. Restore Claude memory + plan from the tarball.
tar xzf claude-faac-state.tgz -C ~

# 6. Sanity check: re-run the standard gate.
TAG=resume ~/lc_diag/lever_sweep.sh   # if harness was tarballed
# Expected: voip=3.216 he64=3.082 lc128=4.395
```

If the harness wasn't preserved, the 12 voip clip names, 9 he64
clip names, and 4 lc128 clip names are listed inline in the
fc78245 commit message body. Recreate from there.
