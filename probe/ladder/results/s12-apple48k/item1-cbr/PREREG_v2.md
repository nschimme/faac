# Item 1 CBR prereg v2: reservoir-centred aim (written before the v2 arm exists)

Evidence: `RESULTS.md`. Aim 1.0 recovers ~0.8 % BD-rate (fill 0.97 -> 0.16 % at 192k) but parks the reservoir near
empty, so 38-72 % of frames bust the cap and re-quantize (+73 % Ir on velvet) and velvet loses 0.07 at 128k.
Master's 0.99 does the opposite: the reservoir saturates and the surplus is stuffed.

Arm `cbrmid` (env-gated, CBR only, unset = master payload-identical): under CBR the per-frame core target becomes
the frame budget plus the reservoir's distance from half capacity, amortized like the balance:
`coreTarget = desbits + (resFill - resCap/2) / RC_BALANCE_AMORT - sbrCharge` (the existing balance/lend terms are
kept). Above half full it spends the surplus as signal before it reaches the stuffing floor; below half it saves
before frames reach the cap. No new constants (half capacity, existing AMORT).

Measurement: as `PREREG.md` (LC CBR 128-320, 77 clips, three decoders, worst cells first, rate within 0.5 %, both BD
methods vs master CBR and vs Apple) plus the mechanism table on the same 10 clips (stuffing, bound retries, minimum
fill) and callgrind on velvet and Bachpsichord 10 s at 192k. Advance to HE CBR, 44.1/32 kHz, mono speech CBR and the
full-dataset runner only if: no cell below -0.10, both BD methods below 0 vs master CBR under all three decoders, rate
within 0.5 %, and velvet Ir within +5 % of master.

## Addendum (mechanism data before any v2 MOS row): `reservoir_stats_mid.tsv`, same 10 clips, CBR
192k: stuffing 0.32 % (master 0.97, aim1.0 0.16), bound retries 4.9 % (9.5 / 38.0), min fill 7.4 %, 0 underflows.
320k: stuffing 0.87 % (1.41 / 0.80), retries 24.2 % (22.5 / 71.5), 0 underflows. callgrind 10 s 192k CBR: velvet
252.8M Ir (master 504.2M, -50 %: fewer cap retries), Bachpsichord 171.8M (+0.3 %). Unset payload-identical 12/12.

## Addendum 2 2026-11-17 (v2 one-sided rule, written before any v2 MOS row was read)
PR #208 (the midpoint rule, unconditional under CBR) failed the CI mos gate: 35 CBR cells below -0.10 at low rates
(16k mono speech 20/24, VoIP 24, 24k mono 28/32, 48k stereo 16/24/40, 32k stereo 16). Reading: below half full the
midpoint rule aims under the frame budget by up to resCap/(2*AMORT), which at low rates is a large share of the frame
and starves bursts. v2 keeps master's aim below half full and only spends the surplus above half:
`if (resFill > resCap/2) coreTarget += desbits - desbits*AIM + (resFill - resCap/2)/AMORT` (no env gate; ABR/VBR
untouched). Validation (benchmark runner, FAAD3, all three FAAC profiles, master vs v2): the 12 failing/neighbour
scenarios 16k_mono_20k/24k, 16k_mono_voip_24k, 24k_mono_28k/32k, 32k_stereo_16k, 48k_stereo_16k/24k/40k/80k/128k/192k.
Pass: no clip cell below -0.10 in any scenario x profile; 48k_stereo_128k/192k LC mean dMOS >= 0; ABR and VBR
payloads identical to master. Fail on any low-rate cell -> scope to stereo LC >= 64 kbps/ch as the owner allowed.
