# S9 HE 48k core decision arms — pre-registered before any 49-clip score (2026-10-01)

Gap being chased: fdk leads FAAC at HE 48k by +0.030 adj (S7-STACK), Apple median +0.030. Arms inject one reference
decision set into FAAC's HE core at the audio-derived alignment, rate loop on; each is read against F at its own pad,
bits-adjusted with the per-clip 40/56 slope. 49 clips.

- An injection must take: arm short-window / M/S share must move to within 10 points of the reference's, else the
  arm is void (not a negative).
- A decision CARRIES the gap if its arm is >= +0.015 adj (half the fdk gap) with W > L. It names the lever to build.
- A decision is NOT the lever if its arm is < +0.005 adj or W <= L.
- Between: partial; only worth building if a cheap rule can be named.
- WS (windows + reference sf inside the rate loop) is a diagnostic only; a loss there is not evidence against sf.
- MOS-per-byte bar (user): a lever that needs > ~1 KB of library code needs >= +0.015 on CI to be considered.
