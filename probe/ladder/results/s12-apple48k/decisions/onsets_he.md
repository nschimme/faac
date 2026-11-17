# Onset-aligned window/TNS comparison (HE)

Onsets = reference events scored by the benchmark's `transient.py` (eligible events, per channel, channels merged when within 64 samples); 77 clips, 697 onsets (965 per-channel events).
Alignment: per stream/clip lag by cross-correlation of decoded (ADTS remux, FAAD2, untrimmed) vs source mono sum, first 10 s; frame k = floor((onset+lag)/L), L = 2048 output samples.
Classes at frame k: `short` = EIGHT_SHORT at k; `long+tns` = long-type (ONLY_LONG/START/STOP) with TNS on (either channel of the onset's channels); `long` = long-type, no TNS; `other` = frame missing.
(LONG_START at k-1 + EIGHT_SHORT at k is a subset of `short`.)

## Class shares (% of onsets)

| stream | onsets | short | long+tns | long | other |
|---|---|---|---|---|---|
| FAAC HE 32k | 697 | 95.0 | 0.0 | 5.0 | 0.0 |
| FAAC HE 48k | 697 | 95.0 | 0.0 | 5.0 | 0.0 |
| Apple HE 32k | 697 | 32.6 | 36.9 | 30.6 | 0.0 |
| Apple HE 48k | 697 | 22.0 | 36.7 | 41.3 | 0.0 |
| FDK HE 32k | 697 | 12.2 | 51.2 | 36.6 | 0.0 |
| FDK HE 48k | 697 | 12.2 | 52.1 | 35.7 | 0.0 |

## Alignment validation: share of onsets with EIGHT_SHORT at frame k+offset (%)

| stream | k-2 | k-1 | k | k+1 | k+2 | any of k-1..k+1 | START at k & SHORT at k+1 |
|---|---|---|---|---|---|---|---|
| FAAC HE 32k | 93.1 | 100.0 | 95.0 | 90.0 | 87.8 | 100.0 | 0.0 |
| FAAC HE 48k | 93.1 | 100.0 | 95.0 | 90.0 | 87.8 | 100.0 | 0.0 |
| Apple HE 32k | 49.5 | 65.3 | 32.6 | 35.7 | 41.3 | 78.8 | 9.8 |
| Apple HE 48k | 33.4 | 67.9 | 22.0 | 18.7 | 30.7 | 72.2 | 3.6 |
| FDK HE 32k | 24.8 | 64.3 | 12.2 | 10.3 | 25.4 | 66.6 | 2.0 |
| FDK HE 48k | 24.8 | 64.3 | 12.2 | 10.3 | 25.4 | 66.6 | 2.0 |

## Per-clip class shares (% short / % long+tns) for each stream

| clip | n | FAAC HE 32k | FAAC HE 48k | Apple HE 32k | Apple HE 48k | FDK HE 32k | FDK HE 48k |
|---|---|---|---|---|---|---|---|
| 01 castanets.wav | 14 | 100.0 / 0.0 | 100.0 / 0.0 | 64.3 / 0.0 | 64.3 / 7.1 | 57.1 / 0.0 | 57.1 / 7.1 |
| 03 eig.wav | 65 | 78.5 / 0.0 | 78.5 / 0.0 | 80.0 / 1.5 | 41.5 / 26.2 | 29.2 / 33.8 | 29.2 / 44.6 |
| 04 Bachpsichord.wav | 1 | 100.0 / 0.0 | 100.0 / 0.0 | 0.0 / 0.0 | 100.0 / 0.0 | 0.0 / 0.0 | 0.0 / 0.0 |
| 08 velvet.wav | 19 | 100.0 / 0.0 | 100.0 / 0.0 | 68.4 / 10.5 | 36.8 / 0.0 | 0.0 / 36.8 | 0.0 / 36.8 |
| 09 Linchpin.wav | 1 | 100.0 / 0.0 | 100.0 / 0.0 | 0.0 / 0.0 | 0.0 / 0.0 | 0.0 / 0.0 | 0.0 / 0.0 |
| 11 You look good to me.wav | 3 | 33.3 / 0.0 | 33.3 / 0.0 | 0.0 / 0.0 | 0.0 / 0.0 | 0.0 / 0.0 | 0.0 / 0.0 |
| 11ff_.wav | 5 | 40.0 / 0.0 | 40.0 / 0.0 | 0.0 / 0.0 | 40.0 / 0.0 | 0.0 / 0.0 | 0.0 / 0.0 |
| 13 fatboy_30sec.wav | 312 | 100.0 / 0.0 | 100.0 / 0.0 | 18.3 / 69.9 | 18.9 / 71.5 | 9.9 / 88.8 | 9.9 / 88.8 |
| 13by_.wav | 1 | 100.0 / 0.0 | 100.0 / 0.0 | 0.0 / 0.0 | 100.0 / 0.0 | 0.0 / 0.0 | 0.0 / 0.0 |
| 17qu_.wav | 11 | 100.0 / 0.0 | 100.0 / 0.0 | 9.1 / 18.2 | 9.1 / 18.2 | 0.0 / 9.1 | 0.0 / 36.4 |
| 18vr_.wav | 2 | 100.0 / 0.0 | 100.0 / 0.0 | 50.0 / 50.0 | 0.0 / 0.0 | 0.0 / 50.0 | 0.0 / 50.0 |
| 19am_.wav | 3 | 100.0 / 0.0 | 100.0 / 0.0 | 33.3 / 33.3 | 33.3 / 0.0 | 33.3 / 33.3 | 33.3 / 33.3 |
| 23ht_.wav | 3 | 66.7 / 0.0 | 66.7 / 0.0 | 100.0 / 0.0 | 66.7 / 0.0 | 66.7 / 0.0 | 66.7 / 0.0 |
| 24-Greensleeves-Korean-male-speech.441.16b48k.wav | 1 | 100.0 / 0.0 | 100.0 / 0.0 | 0.0 / 100.0 | 0.0 / 0.0 | 100.0 / 0.0 | 100.0 / 0.0 |
| 24td_.wav | 13 | 84.6 / 0.0 | 84.6 / 0.0 | 15.4 / 0.0 | 0.0 / 7.7 | 7.7 / 23.1 | 7.7 / 23.1 |
| 25rb_.wav | 7 | 57.1 / 0.0 | 57.1 / 0.0 | 28.6 / 14.3 | 0.0 / 0.0 | 0.0 / 14.3 | 0.0 / 14.3 |
| 26fr_.wav | 12 | 100.0 / 0.0 | 100.0 / 0.0 | 8.3 / 25.0 | 16.7 / 25.0 | 8.3 / 33.3 | 8.3 / 50.0 |
| 27-last-song-drums-and-trampets.441.16b48k.wav | 7 | 100.0 / 0.0 | 100.0 / 0.0 | 0.0 / 71.4 | 14.3 / 14.3 | 0.0 / 28.6 | 0.0 / 28.6 |
| 35_SQAM_glockenspiel_cut.16b48k.wav | 8 | 37.5 / 0.0 | 37.5 / 0.0 | 12.5 / 0.0 | 0.0 / 0.0 | 0.0 / 37.5 | 0.0 / 25.0 |
| 4-Sound-English-male.441.16b48k.wav | 1 | 100.0 / 0.0 | 100.0 / 0.0 | 0.0 / 100.0 | 0.0 / 0.0 | 0.0 / 100.0 | 0.0 / 100.0 |
| 9-Have-big-expensive-car.441.16b48k.wav | 2 | 100.0 / 0.0 | 100.0 / 0.0 | 50.0 / 0.0 | 0.0 / 0.0 | 0.0 / 0.0 | 0.0 / 0.0 |
| Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav | 7 | 85.7 / 0.0 | 85.7 / 0.0 | 28.6 / 0.0 | 0.0 / 0.0 | 0.0 / 14.3 | 0.0 / 14.3 |
| Changes.16b48k.wav | 102 | 100.0 / 0.0 | 100.0 / 0.0 | 43.1 / 7.8 | 32.4 / 1.0 | 18.6 / 23.5 | 18.6 / 18.6 |
| ExitMusic.16b48k.wav | 2 | 100.0 / 0.0 | 100.0 / 0.0 | 0.0 / 0.0 | 0.0 / 50.0 | 0.0 / 50.0 | 0.0 / 50.0 |
| Last_Of_The_Mohicanz__Sample_.16b48k.wav | 33 | 100.0 / 0.0 | 100.0 / 0.0 | 15.2 / 30.3 | 15.2 / 0.0 | 0.0 / 0.0 | 0.0 / 0.0 |
| Only Time.16b48k.wav | 2 | 50.0 / 0.0 | 50.0 / 0.0 | 50.0 / 0.0 | 0.0 / 0.0 | 50.0 / 0.0 | 50.0 / 0.0 |
| Robots_old.16b48k.wav | 23 | 100.0 / 0.0 | 100.0 / 0.0 | 87.0 / 0.0 | 4.3 / 13.0 | 4.3 / 13.0 | 4.3 / 8.7 |
| Shinsho_pool_3min45_4min4.16b48k.wav | 7 | 100.0 / 0.0 | 100.0 / 0.0 | 28.6 / 0.0 | 0.0 / 14.3 | 0.0 / 14.3 | 0.0 / 14.3 |
| SinceAlways.16b48k.wav | 1 | 100.0 / 0.0 | 100.0 / 0.0 | 0.0 / 0.0 | 0.0 / 0.0 | 0.0 / 100.0 | 0.0 / 0.0 |
| clapton_44k.16b48k.wav | 2 | 50.0 / 0.0 | 50.0 / 0.0 | 50.0 / 0.0 | 0.0 / 0.0 | 0.0 / 0.0 | 0.0 / 0.0 |
| girl.16b48k.wav | 11 | 100.0 / 0.0 | 100.0 / 0.0 | 18.2 / 27.3 | 0.0 / 9.1 | 0.0 / 18.2 | 0.0 / 18.2 |
| glk.wav | 2 | 50.0 / 0.0 | 50.0 / 0.0 | 50.0 / 0.0 | 0.0 / 0.0 | 0.0 / 0.0 | 0.0 / 0.0 |
| lob.wav | 1 | 100.0 / 0.0 | 100.0 / 0.0 | 100.0 / 0.0 | 0.0 / 0.0 | 0.0 / 0.0 | 0.0 / 0.0 |
| mof.wav | 10 | 100.0 / 0.0 | 100.0 / 0.0 | 30.0 / 0.0 | 10.0 / 10.0 | 0.0 / 10.0 | 0.0 / 20.0 |
| sandman.16b48k.wav | 2 | 50.0 / 0.0 | 50.0 / 0.0 | 0.0 / 0.0 | 0.0 / 0.0 | 0.0 / 0.0 | 0.0 / 0.0 |
| take_your_finger_frin_my_head.16b48k.wav | 1 | 100.0 / 0.0 | 100.0 / 0.0 | 100.0 / 0.0 | 0.0 / 0.0 | 0.0 / 0.0 | 0.0 / 0.0 |

## TNS usage (all ICS of each stream, 77 clips)

| stream | short ICS | short w/ TNS % | mean filters | mean order | long ICS | long w/ TNS % | mean filters | mean order |
|---|---|---|---|---|---|---|---|---|
| FAAC HE 32k | 39756 | 0.00 | - | - | 30378 | 0.00 | 1.00 | 5.00 |
| FAAC HE 48k | 39756 | 0.00 | - | - | 30378 | 0.00 | 1.00 | 7.00 |
| Apple HE 32k | 5564 | 48.94 | 1.37 | 3.28 | 41402 | 6.11 | 1.00 | 7.17 |
| Apple HE 48k | 6028 | 45.32 | 1.25 | 2.88 | 64184 | 5.24 | 1.00 | 7.87 |
| FDK HE 32k | 3988 | 67.65 | 1.73 | 2.49 | 66216 | 9.30 | 1.29 | 6.99 |
| FDK HE 48k | 3988 | 74.62 | 1.72 | 2.66 | 66216 | 10.85 | 1.25 | 6.79 |
