# S12 scoreboard (48 kHz stereo; FAAC master 3a656f7 + candidates vs Apple and FDK)

Status: open / PR candidate ready / closed negative. Numbers are local measurements (rows under each item dir);
"pending" = not yet measured locally. Updated with every push.

| # | item | rungs | FAAC | Apple | FDK | status | notes |
|---|---|---|---|---|---|---|---|
| 1 | LC BD-rate / mean MOS | 128-320 | ABR: vs Apple 128-320 -2.5..-2.7 % resolved, 192-320 parity. CBR (leaderboard mode): -1.0..-1.5 %, unresolved in every range. vs FDK -14..-23 % both modes. CBR dMOS vs Apple at equal realized rate: +0.021/+0.007/-0.003/+0.002/-0.001 at 128/160/192/256/320 (192: 16 better / 61 worse) | MOS 4.8466/4.9192/4.9580/4.9795/4.9919 | 4.8262/4.8799/4.9266/4.9524/4.9598 | open at 192 and 320 | CBR hits the nominal rate, so the 192/320 deficit is a real (small) coding gap, not rate. Group analysis: no clean separator (`item1-192-320/groups.md`); cutoff screen (-c 20000/22000) queued |
| 2a | NPER LC high (dB) | 96 / 128 | 7.97 / 5.12 (CBR ~same) | 6.38 (gist) / 3.85 | 4.98 @128 | open | STNS v1 6.50 / 4.12 but fails MOS gate; v2 (attack window) scoring next |
| 2b | NPER LC low (dB) | 16-40 | gist 23.8-14.6 | gist ~14 (no local Apple LC <= 96 streams: blocked) | gist 7.7-11.9 | open | local FAAC vs fdk_local baseline queued; STNS v1 at 32/40: 17.75->13.98, 14.30->10.95 (fails MOS gate) |
| 2c | NPER HE-v1 (dB) | 32 / 40 / 48 | 21.1 / 18.4 / 16.5 | 17.0 / 15.5 / 14.9 | 15.4 / 14.8 / 14.4 | open | 32k excess in core (crossover 11.6 kHz); 40/48k in 6-12 kHz; SBR grid not onset-adaptive -> SBR grid redesign (prereg PREREG_sbr_grid.md, implementation running); STNS v1/v2 HE scoring queued |
| 2d | NPER HE-v2 (dB) | 24 / 32 / 48 | 21.0 / 17.0 / 13.3 | 16.7 / 16.0 / 13.6 | 14.6 / 13.4 / 11.9 | open | shares the SBR path; covered by the SBR grid arm |
| 3 | stereo fidelity | LC 16-48; HE-v1 24-64 | HE 32/40/48/64 0.917/0.958/0.977/0.984 | 0.968/0.973/0.977/0.982 | 0.973/0.982/0.985/0.989 | open | FAAC HE 32k: IS on 46 % of CPE bands, M/S 0 % (starved tier); Apple M/S 71 %, IS 18 %; FDK M/S 86 %. Per-clip: more FAAC IS than the reference goes with lower fidelity (rho -0.23..-0.46; `decisions/he_stereo_shares.md`). HE `--joint` screen queued. LC 16-48 not yet measured |
| 4 | P1 | HE-v1 32-64; HE-v2 24-48 | HE 3.132/3.375/3.718/4.143; v2 2.164/2.287/2.472 | HE 2.676/2.871/3.369/3.492; v2 2.074/2.176/2.434 | HE 2.932/3.422/3.836/4.152; v2 2.160/2.286/2.362 (xaac v2 2.274/2.355/2.403) | open | FAAC leads HE P1 at 32k, trails FDK/xaac at 40/48k by 0.05-0.14; HE-v2 trails xaac 0.07-0.11 at 24/32. HE-v1 BD-rate: FAAC leads all refs (-5 to -10 %). Bottom-5 P1 clips at 40/48k are mostly stereo losers (applaud, 18vr_, fatboy; `stage1-rebaseline/he/losers.md`); HE `--joint` screen preregistered (`stage4-stereo/PREREG_he_joint.md`), queued |
| 5 | HE-v2 collapse >=192k | 96-320 | flat 3.636 (22 clips) | n/a | gist flat 3.775 | closed (not on master) | `anomaly-hev2-high/RESULTS.md`; master caps HE-v2 ABR at ~107 kbps; gist build a7d2f755 unavailable |

## Item 2 note
STNS v1 (TNS on every short window): LC pooled NPER -1.0..-3.8 dB at 32-128 (128k 5.12 -> 4.12, Apple 3.85, FDK 4.98), CIs below 0 under all three decoders, but 2-8 cells per rung below -0.10 MOS (13 fatboy -0.59 at 96k): negative as configured (`stage3-transients/RESULTS_stns_v1_lc.md`). v2 = attack window only (`PREREG_stns_v2.md`).

## PR candidates
None yet.
