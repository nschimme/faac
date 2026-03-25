# FAAC Psychoacoustic Spreading Function — Handoff Document

## Project Goal

Implement Task 1 Step 2: a spreading-function simultaneous-masking zero-out in `bmask()` in `libfaac/quantize.c`.
Target: +0.09 voip MOS gain without regression on music scenarios (>−0.05 MOS).

Gate pass criteria:
- voip ≥ 3.5
- vss ≥ 3.5
- music_low ≥ 3.5
- music_std ≥ 4.0 (no regression > −0.05)
- music_high ≥ 4.0 (no regression > −0.05)

---

## Branch

`spec-ath` (local). HEAD = `6d9b02e` (origin/spec-ath has one old commit, ignore it — all work is in
the working tree, NOT yet committed).

---

## Current State of Working Tree

All changes are **unstaged/staged but not committed**. Run `git status` to confirm.

### Files modified:

1. **`libfaac/psy_tables.h`** (new, staged) — ATH table, `bark_from_hz()`, `ath_from_hz()`, `psy_tables_init()` declaration
2. **`libfaac/psy_tables.c`** (new, staged) — Fills `sfb_bark[12][NSFB_LONG]`, `sfb_bark_s[12][NSFB_SHORT]`, etc.
3. **`libfaac/quantize.h`** (modified) — Added `sr_idx` and `spreading` to `AACQuantCfg`
4. **`libfaac/frame.c`** (modified) — `#include "psy_tables.h"`, sets `spreading` and `sr_idx` in `faacEncSetConfiguration()`/`faacEncOpen()`
5. **`libfaac/quantize.c`** (modified) — `#include "psy_tables.h"`, Pass 2 spreading zero-out in `bmask()`
6. **`libfaac/meson.build`** (modified) — `psy_tables.c` and `psy_tables.h` added to `common_src`

---

## Key Design Decisions

### Spreading flag
`AACQuantCfg.spreading = (config->quantqual < DEFQUAL)` — set once at configure time.
- DEFQUAL = 100
- voip (16 kbps) → quantqual ≈ 12.5 → spreading = 1 (active)
- vss (32 kbps) → quantqual ≈ 25 → spreading = 1 (active)
- music_low (64 kbps) → quantqual = 64 → spreading = 1 (active)
- music_std (128 kbps) → quantqual = 100 → spreading = 0 (inactive → bit-identical to baseline)
- music_high (192 kbps+) → quantqual > 100 → spreading = 0 (inactive → bit-identical to baseline)

### Spreading logic in `bmask()` (Pass 2)
```c
if (!spreading) return;   // Gate: high-bitrate scenarios skip entirely

for (sfb = 0; sfb < coderInfo->sfbn; sfb++) {
    float b_t = bark[sfb];
    double M  = 0.0;
    for (m = 0; m < coderInfo->sfbn; m++) {
        float db       = b_t - bark[m];
        float slope_dB = (db >= 0.0f) ? (-25.0f * db) : (10.0f * db);
        M += (double)bandenrg[m] * (MASK_RATIO * powf(10.0f, slope_dB * 0.1f));
    }
    if ((double)bandenrg[sfb] <= M)
        bandqual[sfb] = 0.0;
}
```
`MASK_RATIO = 0.001f` (−30 dB guard; conservative).
Two-slope spreading: −25 dB/Bark upward, +10 dB/Bark downward.

### Critical line that MUST stay in Pass 1
```c
target *= 10.0 / (1.0 + ((faac_real)(start+end)/last));
```
This line is from the baseline `bmask()`. DO NOT remove. Previous attempts that removed it caused music_std and music_high bitstreams to differ from baseline even with spreading=0.

### Bug fixed this session
The original spreading condition was:
```c
if ((double)bandenrg[sfb] <= M && bandenrg[sfb] < nf_thr)   // ← WRONG
```
where `nf_thr = NOISEFLOOR^2 * width * gsize` — exactly the same threshold as `rmsx < NOISEFLOOR` in `qlevel()`. This made the condition redundant (only zeroed bands already zeroed by qlevel), so spreading was a no-op and all 947 MD5 hashes matched baseline.

**Fix**: removed condition `(b)`. Now only `E[sfb] <= M` is required, and spreading fires on 200/400 voip files.

---

## Benchmark Situation

### ViSQOL image issue
The old `baseline.json` was generated with ViSQOL image `15e9ff263163`. The benchmark repo was updated to default to `228d82cc361c`. Comparing across images produces a systematic ~−0.07 MOS bias on music_std even for bit-identical bitstreams (confirmed by running old baseline through new ViSQOL).

**Required**: Generate a fresh baseline with the **same** ViSQOL image as the candidate. Both runs must use the same image tag.

### Docker memory issue (macOS)
The `228d82cc361c` ViSQOL image fails with exit code 137 (OOM-killed) on the current machine. The `15e9ff263163` image works but produces biased scores.

On the new machine:
1. Check if Docker has enough RAM (ViSQOL needs ~4–8 GB)
2. OR check if local ViSQOL Python binary works: `which visqol`
3. Use `--skip-mos` to verify MD5 gate first

### Confirmed results (pinned image, same environment)
Using `baseline_pinned.json` and `candidate_pinned.json` (both with `15e9ff263163`, pre-bug-fix):
```
voip:        +0.082  (but spreading was a no-op — all MD5 identical!)
music_std:   +0.000  (49/49 MD5 match)
music_high:  +0.000  (49/49 MD5 match)
```
Those gains were ViSQOL run-to-run noise.

**Post-bug-fix (spreading_v4)** — MD5 counts with the fix applied:
- voip: 200/400 differ (spreading fires!)
- vss: 74/400 differ
- music_low: 21/49 differ
- music_std: 0/49 differ (correct — spreading=0)
- music_high: 0/49 differ (correct — spreading=0)

Full MOS run for spreading_v4 not yet completed successfully due to Docker OOM.

---

## How to Continue on New Machine

### 1. Clone/pull the repo
```bash
git clone <repo>  # or pull
git checkout spec-ath
# The working tree changes need to be applied manually — see below
```

### 2. Apply the changes
If the working tree changes aren't transferred via git, apply them manually.
The key change is in `libfaac/quantize.c` — remove the dual condition `&& bandenrg[sfb] < nf_thr` from the spreading loop.

The full set of modifications is in files:
- `libfaac/psy_tables.h` (new)
- `libfaac/psy_tables.c` (new)
- `libfaac/quantize.h`
- `libfaac/quantize.c`
- `libfaac/frame.c`
- `libfaac/meson.build`

### 3. Build
```bash
meson setup build --buildtype=release  # if not already
ninja -C build
```

### 4. Run MD5 gate (no Docker needed)
```bash
cd build/faac-benchmark
python3 run_benchmark.py \
    ../frontend/faac \
    ../libfaac/libfaac.dylib \
    v4_gate1 \
    /tmp/v4_gate1.json \
    --skip-mos \
    --scenarios music_std,music_high
```
Verify: 49/49 MD5 match on music_std and music_high vs baseline.json.

### 5. Run full benchmark (same image for both)
```bash
# Step A: baseline (stash changes first)
git stash
ninja -C build
python3 run_benchmark.py ../frontend/faac ../libfaac/libfaac.dylib baseline_v4 /tmp/baseline_v4.json

# Step B: candidate
git stash pop
ninja -C build
python3 run_benchmark.py ../frontend/faac ../libfaac/libfaac.dylib candidate_v4 /tmp/candidate_v4.json

# Step C: compare
python3 compare_results.py /tmp/baseline_v4.json /tmp/candidate_v4.json
```

### 6. Commit when benchmark passes
```bash
git add libfaac/psy_tables.c libfaac/psy_tables.h \
        libfaac/quantize.c libfaac/quantize.h \
        libfaac/frame.c libfaac/meson.build
git commit -m "..."
```

---

## Exact Current Code (for verification on new machine)

### `libfaac/quantize.c` — Pass 2 spreading section (lines ~169–197)
```c
  /* Pass 2: simultaneous-masking zero-out (low-bitrate only).
   * Zero bandqual when E[sfb] is at or below the spreading-function
   * masking threshold M.  MASK_RATIO=0.001 is conservative (~30 dB
   * guard), so only bands that are truly masked by neighbours are
   * suppressed; music content well above the mask is unaffected. */
  if (!spreading)
    return;

  for (sfb = 0; sfb < coderInfo->sfbn; sfb++)
  {
    float b_t = bark[sfb];
    double M  = 0.0;

    for (m = 0; m < coderInfo->sfbn; m++)
    {
      float db       = b_t - bark[m];
      float slope_dB = (db >= 0.0f) ? (-25.0f * db) : (10.0f * db);
      M += (double)bandenrg[m] * (MASK_RATIO * powf(10.0f, slope_dB * 0.1f));
    }

    if ((double)bandenrg[sfb] <= M)
      bandqual[sfb] = 0.0;
  }
}
```

### `libfaac/quantize.h` — AACQuantCfg struct additions
```c
typedef struct
{
    faac_real quality;
    int max_cbl;
    int max_cbs;
    int max_l;
    int pnslevel;
    int sr_idx;      /* GetSRIndex() result — index into psy_tables sfb_bark/sfb_ath */
    int spreading;   /* 1 = apply spreading-function zero-out in bmask (low bitrate only) */
} AACQuantCfg;
```

### `libfaac/frame.c` — additions to faacEncSetConfiguration()
```c
hEncoder->aacquantCfg.spreading = (config->quantqual < DEFQUAL);
```

### `libfaac/frame.c` — additions to faacEncOpen()
```c
hEncoder->aacquantCfg.sr_idx = (int)hEncoder->sampleRateIdx;
psy_tables_init(srInfo, 12);
```

---

## Relevant Files

| File | Purpose |
|------|---------|
| `libfaac/quantize.c` | Pass 2 spreading function in `bmask()` |
| `libfaac/quantize.h` | `AACQuantCfg` struct with `sr_idx`, `spreading` |
| `libfaac/frame.c` | Sets `spreading` and `sr_idx`, calls `psy_tables_init()` |
| `libfaac/psy_tables.h` | Bark scale, ATH table, inline helpers |
| `libfaac/psy_tables.c` | Fills `sfb_bark`, `sfb_ath` arrays |
| `libfaac/meson.build` | Build: includes psy_tables.c/h |
| `build/faac-benchmark/` | Benchmark scripts |
| `build/faac-benchmark/config.py` | Scenario thresholds |
| `build/faac-benchmark/baseline.json` | Original baseline (old ViSQOL image) |

---

## Rules / Lessons Learned

1. **Never remove** `target *= 10.0 / (1.0 + ((faac_real)(start+end)/last));` from `bmask()` Pass 1.
2. **Pin ViSQOL image**: baseline and candidate must use the same image tag for valid comparison.
3. **Check spreading=0 bit-identity** first: all music_std and music_high MD5s must match before running MOS.
4. The `spreading` flag must be set in `faacEncSetConfiguration()`, not `faacEncOpen()`, because it's configuration-dependent.
5. `sr_idx` must be set in `faacEncOpen()` (after `sampleRateIdx` is determined).
