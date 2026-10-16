# LC high bitrates vs Apple: baseline gap and first screen (49 clips, 48 kHz stereo, ABR)

Base: nschimme/faac `he-v1-retune` 19a4b70 (LC output identical to master). Preregistered in `PREREG.md`.

## The gap (decoder-independent)
FAAC vs Apple BD-rate (positive = FAAC needs more bits), mean [95 % CI]:
| rungs | FAAD2 | FDK decoder |
|---|---|---|
| 128/160/192 | +6.89 % [+4.15, +9.78] | +6.96 % [+4.24, +9.82] |
| 96/128/160/192 | +3.79 % [+0.18, +7.37] | +4.01 % [+0.33, +7.62] |
FAAC vs FDK at 128-192 (FAAD2): -10.28 % [-13.31, -7.21] (FAAC well ahead). Mean MOS at equal nominal rung: FAAC - Apple = -0.016/-0.014/-0.014 at
128/160/192 (FDK decoder -0.017/-0.014/-0.014); at 192k Apple wins 47 of 49 clips. Worst clips: velvet, take_your_finger_frin_my_head, fms, Girl_In_The_Fire.
Rungs 256/320 have no packaged Apple reference. Rate accuracy: FAAC lands 0.6-0.8 % under each nominal rate, Apple 0.2-1.2 % over (BD-rate charges actual bits).

## Where the loss is (192 kbps, 16 clips = 8 worst + 8 random, FAAD2; `bands_lc.py`)
FAAC - Apple band MOS: 0-2 kHz -0.004, 2-6 kHz -0.002, **6-12 kHz -0.015, 12-16 kHz -0.019**, 16 kHz+ -0.006. The deficit is in 6-16 kHz; the low bands are at parity.
(FDK is far worse than both above 16 kHz.) Band-limited MOS is a proxy.

## Screen with existing CLI switches (BD-rate vs default over 128/160/192; FAAD2 / FFmpeg / FDK decoder)
| arm | FAAD2 | FFmpeg | FDK decoder |
|---|---|---|---|
| `--no-pns` | +5.25 [+3.71,+6.97] | +5.14 [+3.60,+6.85] | not run |
| **`--joint 1` (M/S only, no intensity stereo)** | **-1.58 [-3.04,-0.46]** | **-1.58 [-3.09,-0.44]** | **-1.56 [-3.09,-0.42]** |
| `--no-pns --joint 1` | +3.78 [+1.38,+6.23] | +3.69 [+1.26,+6.17] | not run |
| `--no-tns` | -0.05 [-0.55,+0.39] | -0.02 [-0.53,+0.44] | not run |
| `-c 20000` | -0.61 [-1.87,+1.13] | -0.68 [-1.96,+1.07] | -0.52 [-1.83,+1.34] |
Reading: intensity stereo costs about 1.6 % at these rates (roughly a quarter of the +6.9 % gap to Apple), consistently across all three decoders, and passes the
preregistered all-decoders rule. PNS is worth about 5 % (it saves more bits than it loses), TNS is neutral, a wider bandwidth is neutral (CI spans 0).
Limits: screened on the same corpus that located the gap (not held out); only 128-192 kbps; the switch is global today, so a production change needs a
bitrate-gated default; 96 kbps and above 192 kbps not tested (no Apple refs above 192); rate and VBR/CBR untested.

## Worst-clip check (the CI gate fails any clip x rung past -0.10 MOS), FAAD2, 49 clips x 3 rungs = 147 cells vs default
- `--joint 1`: 0 of 147 cells below -0.10; worst -0.092 (24-Greensleeves-Korean-male-speech @128k), then -0.024, -0.014, -0.010. Mean MOS at equal rate +0.0015, mean rate change -0.02 kbps.
  The BD-rate gain (-1.6 %) comes from a small, broad shift; the one near-gate clip needs a look before any default change.
- `-c 20000`: 0 of 147 below -0.10, worst -0.011; mean +0.0014.
