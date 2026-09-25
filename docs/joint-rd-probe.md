# Joint spectral/scalefactor ceiling experiment

This is an experimental, default-off probe on the cumulative #228 + #229 +
#230 stack. It is not a production quality claim or a public API addition.

Build with `meson setup b -Drd-probe=true --buildtype=release`. Enable for one
encode with `FAAC_RD_LAMBDA=0.03 b/frontend/faac ...`; an absent variable uses
the original quantizer and section selector. `FAAC_RD_TRACE=1` prints decisions
and the exact spectral/section/scalefactor bit totals per ICS. Invalid lambda
values fail explicitly. A normal build compiles the entire probe out.

Each originally regular spectral band can keep the incumbent or use an
ordinary quantization at an absolute scalefactor within ±2 of its original.
For every covering spectral book, all retain/decrement combinations within
each AAC pair or quadruple are enumerated. Magnitudes may reach zero; their
sign cannot change. PNS and intensity books and decoded scalefactors remain
fixed. Existing zero bands are not converted back to regular bands.

The objective is `sum(weight * (input - reconstructed)^2) + lambda * bits`.
Reconstruction in the quantizer's input coordinates is signed `abs(q)^(4/3)`
divided by `sfac_to_gain(100 + stereo_bias - absolute_sf)`. Before rounding,
the existing quantizer's gain is `target/rms`. Consequently the normalization
is `weight = target^2 / rms^2`, where `rms^2 = band_energy/grouped_band_width`.
The reference energy is captured from the existing measurement: M/S uses
half the weaker L/R energy, masking uses the original L/R group reference,
and intensity's input scaling is removed with its saved scalefactor bias.

The probe retains an absolute cost matrix for every band, including
escape-only bands. Each coordinate candidate prices all spectral tuples,
the actual section runs (including terminating fields at multiples of 7/31),
and every affected scalefactor predictor chain. Global gain is recomputed
from the first remaining regular band. Illegal absolute gains, delta chains,
and first-PNS absolute values are rejected rather than clamped.

Accepted band changes rerun exact joint section selection across each window
group. Coordinate descent visits bands and SF/book candidates in fixed order;
the incumbent wins ties. It stops on convergence or after eight sweeps.
Candidate magnitudes stay anchored to the ordinary quantization at each SF,
so repeated sweeps cannot shave a line without bound. Scratch state belongs
to one `BlocQuant` call and is rebuilt on every rate-control retry.

`meson test -C b --print-errorlogs` checks absolute sizing against actual bit
writing, escape magnitudes through 8191, section length escapes, grouped short
windows, scalefactor limits and an exhaustive section-selection oracle.
`tools/validate_rd.py BASE PROBE FDKDEC OUTPUT` checks probe-off ADTS identity,
active determinism and decoding on synthetic edge cases at 44.1 and 48 kHz.
The active encoder also checks predicted versus written bits on every ICS.

Tune only on the historical eight-clip subset, then freeze candidates before
the 49-clip evaluation. Promotion requires at least +0.03 bits-adjusted ViSQOL
MOS at HE48 or LC96 with both FFmpeg and FDK decoding, corroborating Zimtohrli,
and no aggregate stereo coherence regression. Report individual outliers.
Do not optimize runtime or footprint unless the quality gate passes.
