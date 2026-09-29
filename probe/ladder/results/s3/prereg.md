# Stage S3 pre-registered decision rules (written before each run)

## D1 oracle (2026-09-29 ~19:08 UTC, before scoring)
Base: probe with FAAC_SF_SMOOTH=0.6 FAAC_BS_DROPRATIO=12 (= master+#595+#599, 49/49 PCM). 128k, Apple 128k refs.
Controls: Control 0, KA, KF must be 49/49. Arms rMS, rSF, rSF+rMS vs F (bits-adjusted, base112/144 slope).
Reading: M/S is a live lever alone if rMS >= +0.005 with W>L; live paired with SF if (rSF+rMS) - rSF >= +0.005 with W>L.

## D2 rule sweep (2026-09-29 ~19:12 UTC, before scoring)
Probe knobs FAAC_MS_Q in {1.25,1.6,2,3} (threshold on EmEs/(ElEr/4); 1 = production) and FAAC_MS_LRDB in {6,10,15}
(M/S iff |L/R| < X dB). Rate loop on, 128k ABR, 49 clips, adj vs ctl (knob unset) with the base's own s112/s144 slope.
Pass: mean >= +0.005, W > L (|d|>0.0005), no clip < -0.05, total bytes within +-12.5 %, chosen value at the bracket centre
(both neighbours lower). Control: knob unset = master+#595+#599 PCM (49/49, checked).

## C3 window pass 2 (2026-09-29 ~19:16 UTC, before scoring)
Base as D. New probe knob FAAC_BS_RESET=b (after a rise, level := max(level, b*e); unset/0 = production).
Arms: RESET b in {0.25,0.5,1.0}; context (FAAC_BS_PREVS,FAAC_BS_NEXTS) in {(1,2),(2,1),(1,1)} (production 2,2);
FAAC_BS_MINE in {1e5,1e6,1e7} (sub-block energy floor for a rise; production 0).
Rule (as S2): mean adj >= +0.005, W > L, no clip < -0.05, bracket centre; sentinels girl, Last_Of_The_Mohicans,
liberate, take_your_finger reported per arm. Control = knob unset = master+#595+#599 PCM (49/49, checked).

## D1b oracle amendment (2026-09-29 ~19:21 UTC, before scoring the new arms)
g_make's rMS / rSF+rMS flip the M/S bit on every band, including FAAC IS bands (the bit inverts intensity phase) and
PNS bands; first clips scored rMS -0.27 and rSF+rMS down to 2.74 MOS, so that construction is invalid and is not read.
New arms (s3/d_make.py), only bands regular in both channels of both streams, same window layout:
rMSr (Apple M/S bit), rSFr (Apple sf on those bands), rSFMSr (both). Reading, vs F, bits-adjusted:
M/S alone live if rMSr >= +0.005 with W>L; M/S live given Apple sf if rSFMSr - rSFr >= +0.005 with W>L.
