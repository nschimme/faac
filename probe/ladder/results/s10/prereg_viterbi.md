Pre-registered 2026-10-06 before computing.
Rule: per ICS, PNS energies in decode order (all window groups chained). Each band may move within +-t steps (1.5 dB each) of its
energy; choose the path minimising book12 delta bits; first PNS band stays 9-bit absolute. Variants: t=1, t=2, and FD (t=1 below 6 kHz, t=2 above).
Control: t=0 reproduces S-line sf_pns on every ICS.
Report per rate: bits saved/frame and % of frame, for long (default at 32/48, HE_NOSO at 16/24) and short (default at 16/24/32/48).
Stop: free ceiling = %bytes saved x measured MOS/%byte slope; proceed to a probe knob if >= +0.015 at any of 16/24/32k, else close.
