# Crossover probe handoff (stopped on request)

## State and gates

- Worktree /private/tmp/claude-501/faac-work/xover-gate, branch xover-gate, base c795effe6c96859636bb2e3b111739f46a65bca1; PR #572 head 1ca87cb1 merged with --no-commit --no-ff. No commit or push.
- Stopped the serial benchmark with Ctrl-C during clip 27, 40 kbps, S13 hybrid construction. Exit code 130. Completed blocks have done markers; resume reruns the unfinished block.
- Five-clip off-knob bitstream identity: PASS 20/20 across 40/48/56/64; SHA-256 pairs in probe/xover_identity/results.csv. probe/xover_identity.py compares this build to the price-grid build.
- Full FAAD3 master/high/low SBR edge arrays: PASS all four rates on Robots_old; F and N13 T records in probe/xover_gate/{fdk,faac}{rate}.tables.dump. The running harness repeated equality for every completed N13/G13 block. Noise edges intentionally differ.
- Header: PASS except allowed noise_bands. F noise_bands = 2 at 40, 3 at 48/56/64; N13/G13 = 0 (one derived noise band). Other compared fields and kx/M/hi/lo counts match. See H records in each probe/xover_run/*/{rate}/{F,N13,G13}/sbr.dump.
- Core crossover: PASS on all completed blocks. Harness requires mean FAAD3 C max_sfb of N13 below N15. Encoder bandwidth on Robots: N15 12000 Hz; N13 7031/8250/9000/10500 Hz at 40/48/56/64. Robots mean C max_sfb N15 33.93; N13 28.56/30.15/31.15/32.34. Evidence: probe/xover_gate/N{15,13}{rate}.dump.
- N13/G13 strict FAAD decode: PASS, zero concealment and zero non-END termination for all completed blocks. FFmpeg strict decode passed. G13 donor grids: 100% both channels. Harness checks FAAC writer/FAAD bit splits and P-record fill boundaries.
- Band-swap N13-vs-N13 self-recombine: PASS for first five clips × four rates × two decoders, absolute MOS bias below 0.02. Bias values are in N13 rows of probe/xover_run/scores.csv. Uses bandswap.py align_to_ref and fir_split.
- No gate failed. Tables A-C are incomplete; probe/XOVER_RESULT.md is the earlier stopped-gate draft and must be replaced by the analyzer after full completion.

## Completed output and partial MOS

- probe/xover_run/scores.csv: 520 rows = 26 clips × four rates × five arms (N15/N13/G13/S13/F). Completed clip indices 00–25 in sorted corpus order; each probe/xover_run/{index}_{stem}/{rate}/done exists (104 markers). Each block holds arm AAC, FAAD dump, FF/fdk WAV, writer dump, F padded donor, and S13-{ff,fdk}.wav.
- Interrupted directory: probe/xover_run/26_SlavesOfFear.16b48k/40 has partial N15/N13/G13/F outputs, no S13 and no done; resume recomputes this block. scores.csv has no clip-27 rows.
- Partial means below are raw MOS over 26 clips only, not final results. Each cell is FF decode / fdk decode.

| kbps | N15 | N13 | G13 | S13 | F |
|---:|:---|:---|:---|:---|:---|
| 40 | 4.0586/4.0487 | 3.9911/3.9364 | 3.9778/3.9142 | 4.0351/4.0058 | 4.1358/4.0978 |
| 48 | 4.1981/4.1828 | 4.1072/4.0606 | 4.0991/4.0483 | 4.1459/4.1241 | 4.2190/4.1997 |
| 56 | 4.3297/4.3139 | 4.3010/4.2598 | 4.3001/4.2429 | 4.3471/4.3267 | 4.4343/4.4125 |
| 64 | 4.4387/4.4280 | 4.4177/4.4004 | 4.4163/4.3920 | 4.4496/4.4411 | 4.5228/4.5121 |

## Resume exactly

    cd /private/tmp/claude-501/faac-work/xover-gate
    CCACHE_DISABLE=1 meson compile -C build
    /Users/nschimme/gitprojects/faac-benchmark/.venv/bin/python probe/xover_run.py
    /Users/nschimme/gitprojects/faac-benchmark/.venv/bin/python probe/xover_coherence.py
    /Users/nschimme/gitprojects/faac-benchmark/.venv/bin/python probe/xover_analyze.py

- xover_run.py is serial, resumes at missing done markers, and sets NUMBA_DISABLE_JIT=1 for benchmark imports. --limit 5 was the five-clip gate; full invocation resumes from clip 27. Corpus is /Users/nschimme/gitprojects/faac-benchmark/data/external/audio (49 stereo 48 kHz WAVs).
- Per-rate FAAC knobs (START,STOP,FREQ_SCALE,ALTER): 40 (10,9,2,1), 48 (12,9,2,1), 56 (13,10,1,1), 64 (14,12,1,1). XOVER=0 is default. Donor fdk command: fdkaac -p 5 -b RATE*1000 -f 2 on a WAV with 33 zero samples prepended. Injection: FAAC_SBR_INJECT=F/sbr.dump FAAC_SBR_INJECT_FIELDS=grid FAAC_SBR_INJECT_OFFSET=1; FAAC frame n maps to fdk n+1. S13 uses N13 low + F high at kx*375 Hz via bandswap.py FIR; kx = 18/22/24/27.
- Scripts: probe/xover_identity.py, probe/xover_run.py, probe/xover_coherence.py, probe/xover_analyze.py. Source patch probe/xover.patch; FAAD3 table-dump patch probe/faad-xover.patch. All are uncommitted. FAAD3 worktree at /private/tmp/claude-501/faac-work/faad3 also has uncommitted T-record instrumentation. libfaac/sbr.c is modified; merge is staged and knobs are unstaged (MM). Probe outputs and scripts are untracked.

## Pitfalls

- Earlier XOVER_RESULT.md incorrectly says fdk noise_bands is 3 at 40; actual H record says 2. Analyzer will overwrite the draft.
- Do not run benchmark jobs concurrently. Do not read fdk-aac source. FAAD_DUMPTAB=1 is required for T records; xover_run.py supplies it.
- xover_analyze.py and xover_coherence.py were written but not yet executed or verified. Check their output and all 980 score rows before accepting Tables A-C. S13 has no bitstream; analyzer uses N13 bytes as its nominal bit-adjustment proxy.
- Some S13 hybrids score below N13 despite passing self-recombine and lag checks. Report measured result, including negative N13→S13 gaps.
