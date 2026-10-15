# Next experiments toward Apple and FDK parity

Update 2026-10-15: the completed reference matrix at encoder `367cf8dc`
locates the main remaining target at **48 kHz stereo HE 40–64 kbps**;
48 kHz LC 128–192 kbps against Apple is secondary. Follow the
[focused experiment proposals](reference-gaps-367cf8dc/SUGGESTED_EXPERIMENTS.md)
and their comparability/provenance limits before implementing anything.
The [Apple reference package](../../ref/apple-s11-367cf8dc/README.md) supplies
the existing 49-clip HE40/48/56/64 and LC96/128/160/192 streams for Linux
cloud analysis. Those references are not a held-out set. New held-out
candidate-versus-FAAC checks can run without new Apple encodes; a held-out
claim against Apple needs corresponding Apple references. The research
sequence below is retained as rationale, not a report of additional runs.

This is a proposed research sequence, not measured evidence or a new quality
claim. Read Stage S10 in `../../LADDER_RESULT.md`, the Stage S11 ledger, and
the recorded limitations before running it. The old long-block plan is
superseded. This probe branch records research; do not merge its encoder or
injector code into production. Production candidates start independently from
current `FreewareAdvancedAudio/faac:master` and go to `nschimme/faac` first.

## 1. Rebuild the reference scoreboard on the current baseline

The study establishes improvements over FAAC controls; it does not establish
that current FAAC beats Apple or FDK. Stage S10's reference gaps belong to an
older encoder, metric and decoder configuration. Do not subtract the new
BD-rate improvement from an old MOS gap.

Freeze current upstream, the candidate from fork PR #70 if still relevant,
faac-benchmark, both reference encoder versions, scoring decoder and metric
helpers. Re-run the standard CI scenarios and the established LC/HE reference
ladders on the same original PCM inputs. Use the same decoder for each paired
comparison, corrected bounded alignment, actual AAC packet bitrate, and
per-clip PCHIP BD-rate with clip-bootstrap intervals. Record ABR, CBR and VBR
separately. Keep the existing 49-clip corpus as a confirmation corpus and add a
separate, preregistered held-out set before selecting another policy.

Assert the actual profile, output sample rate, channels and decoded length of
every reference. Stage S10 found that Apple's nominal 32 kbps output was
32 kHz, not the compared 48 kHz scenario. Either obtain a genuinely matched
reference or report that scenario separately; do not silently resample only
one arm and call it equivalent. Likewise force HE-AAC v1 when measuring SBR;
the historic automatic LC switch above 56 kbps invalidates an assumed HE
ladder. Keep low-rate HE v2 results separate from HE v1.

Deliverables: per-clip raw rows, a full scenario scoreboard against both
references, confidence intervals, worst cases, exact commands, source/tool
hashes, and a list of remaining statistically resolved gaps. Pick the next
target from that table. A changed decoder must pass a paired PCM/length audit
or both sides must be rescored; an encoder hash match alone does not validate
cached MOS.

## 2. Explain the fixed-border win before adding a selector

Fixed midpoint borders improved FAAC BD-rate by 0.5126% on the merged
baseline. The full adaptive tests improved over upstream but did not resolve
an extra gain over midpoint borders. Their quantized-energy distortion proxy
is therefore not demonstrated to select better perceptual outcomes than the
cheap fixed rule. Do not promote the heavy probe because it can minimize its
own proxy.

First audit time coordinates without tuning quality: inject impulses, steps
and short bursts at every phase across adjacent frames; log detector slot,
QMF history/input origin, sampled energy bins, envelope borders and emitted
frame identity. Repeat at decimation 1 and 4, mono/stereo, including retry and
partial-frame cases. Derive expected positions analytically. The cross-frame
work already showed why successor QMF slots 0-5 cannot be assumed to contain
new PCM; carry that lesson into this audit.

If there is a reproducible coordinate error, preregister one derived
correction and its expected impulse behavior. Compare it directly against
both the current baseline and midpoint policy, with unchanged detector,
bitrate and other SBR settings. Run the usual unset identity, decoder,
49-clip and held-out gates. If coordinates are correct, close the hypothesis;
do not turn this into another border/threshold sweep. This audit is proposed,
not a claim that the current encoder has an alignment bug.

## 3. Reprice the parked quantizer winner only if a residual remains

Before choosing a core target, use the refreshed scoreboard to distinguish
the old HE32 SBR ceiling from the old HE48 core ceiling. Stage S6-X found
that reference SBR at a matched low crossover could help at HE32, whereas
reference core quality carried most of the HE48 gap. Those are historical
splice-oracle results, not today's decomposition.

If the HE32 low-crossover SBR ceiling remains on the new baseline, a narrowly
preregistered interaction test is now motivated: hold noise18, limiter and
the new envelope policy fixed, and compare one reference-matched crossover
against the current crossover, with exact source/payload controls and current
reference splice controls. S11 changed the temporal envelope representation
that S6-N explicitly left unresolved. This is new causal context for a
matched interaction, not permission to repeat the old crossover/PNS/threshold
sweeps. Stop if the refreshed oracle or the one-factor gate fails. Do not
attribute a change in core bandwidth to better SBR without the matched
decomposition.

Stage S10 closed long-only windows, PNS/crossover/threshold combinations,
treble slopes and the side-information/free-bits line. Earlier work also
closed fitted scalefactor rules, per-line RD and path-search/DP variants.
Do not rerun those hypotheses without new causal evidence.

The existing per-band sf +/-1 search was parked for cost: Stage S10 records
HE 24-56 kbps CI gains of +0.013 to +0.026 MOS at roughly +5.6 KB and 7-17%
less throughput on its historical base. These are historical measurements,
not a predicted gain on today's baseline. First remeasure that exact parked
candidate on the refreshed target ladder if the scoreboard still identifies
the same quantizer gap. Stop if its quality ceiling no longer clears the
probe's MOS-per-byte bar (historically +0.015 for more than about 1 KB).

If the ceiling survives, profile the existing search. Investigate avoiding
provably redundant work while preserving its chosen candidate and emitted
packets. This is a cost-reduction investigation of an already measured
algorithm, not permission to restart new DP/classifier/search sweeps. Require
exact candidate/bitstream identity to the parked algorithm, matched native
serial timings, ROM/state/scratch accounting, and current CI quality before
proposing production code. Use deterministic FAAC-local decisions; external
encoders remain black-box references.

## Promotion and stopping rules

Preregister the hypothesis, corpus, contexts, metrics and stopping rule before
scoring. Charge every bitrate delta. Report inconclusive intervals as
inconclusive; neither a positive mean nor a four-clip loss proves a small
effect absent. A promising cheap screen needs full-corpus confirmation;
small uncertain effects with prior full-context evidence may justify a
larger matched run, as Stage S11's adaptive attribution did.

Keep independent PRs for independently justified production changes. Test a
combined PR only when interaction measurements justify it, and target the
fork first. Record rejected and incomplete probes alongside winners, with
exact evidence and missing gates. Never infer a new reference victory from a
baseline-only improvement, a passing decoder from an exit code that hides
concealment, or a speed win from an emulated footprint build.
