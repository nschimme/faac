# S12 scoreboard (48 kHz stereo; FAAC master 3a656f7 + candidates vs Apple and FDK)

Status: open / PR candidate ready / closed negative. Numbers are local measurements (rows under each item dir);
"pending" = not yet measured locally. Updated with every push.

| # | item | rungs | FAAC | Apple | FDK | status | notes |
|---|---|---|---|---|---|---|---|
| 1 | LC BD-rate / mean MOS | 128-320 | vs Apple 128-192 -2.8 % (PCHIP) / -2.6 % (linear), CIs exclude 0; **192-320 parity**: PCHIP -2.8 % but linear -1.2 % [-3.2,+0.7], 34/76 clips worse; vs FDK -15 to -23 %. MOS 4.8668/4.9259/4.9553/4.9815/4.9907 | 4.8466/4.9192/4.9580/4.9795/4.9919 | 4.8262/4.8799/4.9266/4.9524/4.9598 | 128-192 reached; 192-320 open | Apple's nominal-rung edge at 192/320 coincides with FAAC's 1.7-2.2 % lower realized rate (ABR); leaderboard runs FAAC CBR, LC CBR queued; `stage1-rebaseline/RESULTS.md` |
| 2 | pre-echo NPER | LC 96/128; LC<=40; HE-v1 24-48; HE-v2 24/32 | LC 128: 5.12; 160: 2.79 | LC 128: 3.85; 160: 2.88 | LC 128: 4.98; 160: 3.23 | open | lead: FAAC has no TNS on short windows (tns.c long only); Apple TNS on 36 % of short ICS, FDK 76 % (`decisions/stereo_shares.md`); onsets: FAAC short at 95 % of scored onsets (Apple 85 %, FDK 56 %), so windows are not the gap; short-window TNS arm preregistered (`stage3-transients/PREREG.md`), patch in progress |
| 3 | stereo fidelity | LC 16-48; HE-v1 24-64 | pending | pending | pending | open | Apple uses IS on 11 % (128k) / 4 % (192k) of CPE bands, FAAC 3.5 % / 0 % |
| 4 | P1 HE-v1/HE-v2 | 24-64 | pending | pending | pending | open | stage 1 HE queued |
| 5 | HE-v2 collapse >=192k | 96-320 | flat 3.636 (22 clips) | n/a | gist flat 3.775 | closed (not on master) | `anomaly-hev2-high/RESULTS.md`; master caps HE-v2 ABR at ~107 kbps; gist build a7d2f755 unavailable |

## PR candidates
None yet.
