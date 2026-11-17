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
