# S12 scoreboard (48 kHz stereo; FAAC master 3a656f7 + candidates vs Apple and FDK)

Status: open / PR candidate ready / closed negative. Numbers are local measurements (rows under each item dir);
"pending" = not yet measured locally. Updated with every push.

| # | item | rungs | FAAC | Apple | FDK | status | notes |
|---|---|---|---|---|---|---|---|
| 1 | LC BD-rate / mean MOS | 128-320 | vs Apple 128-192 -2.8 % (PCHIP) / -2.6 % (linear), CIs exclude 0; **192-320 parity**: PCHIP -2.8 % but linear -1.2 % [-3.2,+0.7], 34/76 clips worse; vs FDK -15 to -23 %. MOS 4.8668/4.9259/4.9553/4.9815/4.9907 | 4.8466/4.9192/4.9580/4.9795/4.9919 | 4.8262/4.8799/4.9266/4.9524/4.9598 | 128-192 reached; 192-320 open | Apple's nominal-rung edge at 192/320 coincides with FAAC's 1.7-2.2 % lower realized rate (ABR); leaderboard runs FAAC CBR, LC CBR queued; `stage1-rebaseline/RESULTS.md` |
| 2 | pre-echo NPER (dB, lower better) | LC 96/128; LC<=40; HE-v1 24-48; HE-v2 24/32 | LC 128 5.12; HE 32/40/48 21.1/18.4/16.5; HE-v2 24/32 21.0/17.0 | LC 128 3.85; HE 17.0/15.5/14.9; HE-v2 16.7/16.0 | LC 128 4.98; HE 15.4/14.8/14.4; HE-v2 14.6/13.4 | open (largest gap) | FAAC already short at 95 % of onsets; lacks short-window TNS (Apple 36-49 %, FDK 68-77 % of short ICS). STNS arm preregistered, screen queued; LC <=40 not yet measured |
| 3 | stereo fidelity | LC 16-48; HE-v1 24-64 | HE 32/40/48/64 0.917/0.958/0.977/0.984 | 0.968/0.973/0.977/0.982 | 0.973/0.982/0.985/0.989 | open | FAAC HE 32k: IS on 46 % of CPE bands, M/S 0 % (starved tier); Apple M/S 71 %, IS 18 %; FDK M/S 86 %. LC 16-48 not yet measured |
| 4 | P1 | HE-v1 32-64; HE-v2 24-48 | HE 3.132/3.375/3.718/4.143; v2 2.164/2.287/2.472 | HE 2.676/2.871/3.369/3.492; v2 2.074/2.176/2.434 | HE 2.932/3.422/3.836/4.152; v2 2.160/2.286/2.362 (xaac v2 2.274/2.355/2.403) | open | FAAC leads HE P1 at 32k, trails FDK/xaac at 40/48k by 0.05-0.14; HE-v2 trails xaac 0.07-0.11 at 24/32. HE-v1 BD-rate: FAAC leads all refs (-5 to -10 %). Bottom-5 P1 clips at 40/48k are mostly stereo losers (applaud, 18vr_, fatboy; `stage1-rebaseline/he/losers.md`); HE `--joint` screen preregistered (`stage4-stereo/PREREG_he_joint.md`), queued |
| 5 | HE-v2 collapse >=192k | 96-320 | flat 3.636 (22 clips) | n/a | gist flat 3.775 | closed (not on master) | `anomaly-hev2-high/RESULTS.md`; master caps HE-v2 ABR at ~107 kbps; gist build a7d2f755 unavailable |

## PR candidates
None yet.
