# Benchmark scorer provenance audit (read-only)

Audit date: 2026-10-14. No encoding, decoding, scoring, or history rewrite was performed.

## Finding

The bounded-lag change landed in faac-benchmark commit `5a00e653c31e8281b24565be1f2d1ce283a98ec9`. The current `phase2_mos.py` SHA-256 is `374ecb752727f85323aeb2f2e6694ebbe7c0f0f33a48c8e947e799501d11278b`; the pre-fix file at `5a00e65^` hashes to `d7e561305fc5089d7bc20a57a01800da716edda6fec7bdcb1318143836f6374d`. Commit `adef778d6f96ea6ce24448fde962f73dc09d012f`, used by the archived HE-v1 quality configs, is a descendant of `5a00e65`; its helper is identical to current. Therefore the bounded-lag bug does not affect the currently archived quality-family MOS results based on their recorded provenance. The fresh fixed8 scorer at commit `374ecb...` is also post-fix. The isolated mode1 gate records benchmark HEAD `bdce18f...` (also post-fix).

| Quality family | Recorded benchmark commit | Lag helper provenance | Decoder recorded for MOS rows | Lag-fix impact | Rescore artifacts |
|---|---|---|---|---|---|
| adaptive | `adef778d` | commit-descended, post-5a; current helper SHA `374ecb...` | FDK `32528021...` (old binary) | Not affected by unbounded lag, based on provenance | Existing configs/results retained; source WAV/cache paths should be checked per exact rows before rescore |
| crossframe-border | `adef778d` | post-5a | FDK `32528021...` | Not affected | Full rows: 1,176/1,176 decoded WAV paths exist; gate rows: 320/320 exist |
| envelope-count | `adef778d` | post-5a | FDK `32528021...` | Not affected | Structural synthetic rows: 168/168 decoded WAV paths exist; production-family artifacts/configs retained |
| full / gate feature stacks | `adef778d` | post-5a | FDK `32528021...` where recorded | Not affected | Config/results retained; per-row output-path availability not exhaustively checked |
| grid | `adef778d` | post-5a | FDK `32528021...` | Not affected | Config/results retained; per-row output-path availability not exhaustively checked |
| harmonic | `adef778d` | post-5a | Binary hash not embedded in its config located; family result artifacts retained | Not affected | Config/results retained; exact WAV availability not exhaustively checked |
| invf | `adef778d` | post-5a | FDK `32528021...` | Not affected | Config/results retained; exact WAV availability not exhaustively checked |
| localnoise | `adef778d` | post-5a | FDK `32528021...` | Not affected | Config/results retained; exact WAV availability not exhaustively checked |
| stack | `adef778d` | post-5a | FDK `32528021...` | Not affected | Config/results retained; exact WAV availability not exhaustively checked |
| tables | `adef778d` | post-5a | FDK `32528021...` | Not affected | Config/results retained; exact WAV availability not exhaustively checked |
| transient-border (historical) | `adef778d` | post-5a | FDK `32528021...` | Not affected | Gate rows: 192/192; fixed8 v1: 684/684; fixed8 v2: 784/784; mode1 final: 1,568/1,568 decoded paths exist |
| transient-border (upstream fixed8 validation) | `adef778d` | explicit `phase2_mos.py` SHA `374ecb...` in combo provenance | Separate provenance; FDK decoder hash not included in that combo provenance | Not affected | Full49 rows: 392/392 decoded paths exist |
| transient-border (upstream 098 mode1 gate) | `bdce18f7123f51c31795afe9fb4571c18f507a77` | post-5a by commit ancestry | FDK `32528021...` | Not affected | 32 active rows plus 16 control rows and exact identity manifest retained; not a MOS study yet |

`32528021...` is a stale/pre-current FDK executable identity, but that alone does not establish quality-score invalidity. The `bdce18f` FAAD2 flush/gapless fix is a separate decoder change and should not be used to infer that these FDK-decoded HE-v1 MOS rows changed. Whether new FDK binaries produce identical PCM remains unverified. Whether new FDK binaries produce byte-identical PCM remains an open empirical question. If they do, MOS recomputation can use retained decoded WAVs without re-encoding. No decoder or MOS recomputation was attempted here.

## Exact config files searched

Each path below records benchmark commit `adef778d6f96ea6ce24448fde962f73dc09d012f` unless noted. The commit is a descendant of `5a00e65`; these are commit-based provenance claims, not individually embedded helper hashes. The explicit helper hash appears only in the combo provenance file listed separately.

- adaptive: `adaptive/results-all/noise-config.json`, `adaptive/results-gate/noise-config.json`
- crossframe-border: `crossframe-border/full-x2-future0-lam003/config.json`, `crossframe-border/gate-v2/config.json`
- envelope-count: `envelope-count/controls-reviewed/envcount-config.json`, `envelope-count/full-active-lambda/envcount-config.json`, `envelope-count/gate-active-lambda/envcount-config.json`
- full/gate feature stacks: `full/feature-config.json`, `gate/feature-config.json`
- grid: `grid/full/grid-config.json`, `grid/gate/grid-config.json`
- harmonic: `harmonic/full/harmonic-config.json`, `harmonic/gate/harmonic-config.json`
- invf: `invf/full/invf-config.json`, `invf/gate/invf-config.json`
- localnoise: `localnoise/full/localnoise-config.json`
- stack: `stack/results-all/stack-config.json`, `stack/results-gate/stack-config.json`
- tables: `tables/gate/tables-config.json`, `tables/results-gate/tables-config.json`
- transient-border: `transient-border/fixed-split-gate-v1/config.json`, `transient-border/fixed8-full-v1/config.json`, `transient-border/fixed8-full-v2/config.json`, `transient-border/full-mode1-final/config.json`, `transient-border/gate/config.json`, `transient-border/gate-final-v2/config.json`
- current isolated mode1 gate: `transient-border/upstream098-mode1-gate/manifest.json` records benchmark commit `bdce18f7123f51c31795afe9fb4571c18f507a77`.
- explicit helper evidence: `transient-border/upstream-fixed8-validation/combo-fixed8-v2-provenance.json` records `phase2_mos.py` SHA-256 `374ecb752727f85323aeb2f2e6694ebbe7c0f0f33a48c8e947e799501d11278b` and benchmark commit `adef778...`.

Additional JSON files under the study tree include timing configs and synthetic summaries; those are not treated as MOS scoring configs. The recursive search found no quality config whose recorded benchmark commit predates `5a00e65`. The artifact availability counts above cover only listed `rows.json` files with decoded paths; this does not establish availability for all result families.
