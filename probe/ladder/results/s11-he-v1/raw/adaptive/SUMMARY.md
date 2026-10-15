# Full-corpus adaptive noise comparison

Corpus: 49 clips at 32, 48, 64, 96 kbps. All rows use strict FDK decoding and Zimtohrli.
BD-rate compares adaptive18 directly with each static control; negative favors adaptive18. Confidence intervals bootstrap over clips.

| Comparison | Mean BD-rate (95% CI) | Median | Packet ties | PCM ties |
|---|---:|---:|---:|---:|
| adaptive18_vs_static18 | +0.067% [+0.011%, +0.142%] | +0.000% | 151/196 | 159/196 |
| adaptive18_vs_static24 | +0.086% [+0.025%, +0.162%] | +0.005% | 0/196 | 0/196 |

## Per-rate paired MOS

| Comparison | Rate | Reference MOS | Adaptive18 MOS | MOS delta (95% CI) | Actual bitrate delta |
|---|---:|---:|---:|---:|---:|
| adaptive18_vs_static18 | 32 kbps | 3.84775 | 3.84715 | -0.00060 [-0.00169, +0.00022] | -0.001% |
| adaptive18_vs_static18 | 48 kbps | 4.20965 | 4.20905 | -0.00060 [-0.00150, +0.00002] | +0.001% |
| adaptive18_vs_static18 | 64 kbps | 4.44459 | 4.44433 | -0.00026 [-0.00059, -0.00000] | -0.000% |
| adaptive18_vs_static18 | 96 kbps | 4.68425 | 4.68445 | +0.00020 [-0.00003, +0.00059] | +0.000% |
| adaptive18_vs_static24 | 32 kbps | 3.84787 | 3.84715 | -0.00071 [-0.00180, +0.00013] | -0.001% |
| adaptive18_vs_static24 | 48 kbps | 4.20986 | 4.20905 | -0.00081 [-0.00173, -0.00017] | +0.001% |
| adaptive18_vs_static24 | 64 kbps | 4.44453 | 4.44433 | -0.00020 [-0.00057, +0.00010] | -0.000% |
| adaptive18_vs_static24 | 96 kbps | 4.68432 | 4.68445 | +0.00013 [-0.00014, +0.00054] | +0.000% |

## Per-clip BD-rate

| Comparison | Clip | BD-rate |
|---|---|---:|
| adaptive18_vs_static18 | 12-German-male-speech.441.16b48k.wav | +0.000% |
| adaptive18_vs_static18 | 15-Good-evening.441.16b48k.wav | +0.000% |
| adaptive18_vs_static18 | 21-classic.441.16b48k.wav | +0.000% |
| adaptive18_vs_static18 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav | +0.000% |
| adaptive18_vs_static18 | 25-This-is-the-end.441.16b48k.wav | +0.000% |
| adaptive18_vs_static18 | 27-last-song-drums-and-trampets.441.16b48k.wav | +0.000% |
| adaptive18_vs_static18 | 35_SQAM_glockenspiel_cut.16b48k.wav | +0.562% |
| adaptive18_vs_static18 | 4-Sound-English-male.441.16b48k.wav | +0.000% |
| adaptive18_vs_static18 | 9-Have-big-expensive-car.441.16b48k.wav | +0.000% |
| adaptive18_vs_static18 | Asleep__4.11-4.31_.16b48k.wav | +0.000% |
| adaptive18_vs_static18 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav | +0.000% |
| adaptive18_vs_static18 | Changes.16b48k.wav | +0.000% |
| adaptive18_vs_static18 | Coral.16b48k.wav | +0.000% |
| adaptive18_vs_static18 | ExitMusic.16b48k.wav | +0.000% |
| adaptive18_vs_static18 | Girl_In_The_Fire__Sample_.16b48k.wav | +0.000% |
| adaptive18_vs_static18 | Greatest_Love_of_All_2min57.16b48k.wav | +0.000% |
| adaptive18_vs_static18 | Hey Tonight.16b48k.wav | +0.000% |
| adaptive18_vs_static18 | Hotel California.16b48k.wav | +0.000% |
| adaptive18_vs_static18 | Jupiter, the Bringer of Jolity.16b48k.wav | +0.000% |
| adaptive18_vs_static18 | Last_Of_The_Mohicanz__Sample_.16b48k.wav | +0.647% |
| adaptive18_vs_static18 | NewYorkCity.16b48k.wav | +0.000% |
| adaptive18_vs_static18 | Only Time.16b48k.wav | +0.000% |
| adaptive18_vs_static18 | Robots_old.16b48k.wav | +0.000% |
| adaptive18_vs_static18 | Severance__1.31-1.51_.16b48k.wav | +0.000% |
| adaptive18_vs_static18 | Shinsho_pool_3min45_4min4.16b48k.wav | +0.000% |
| adaptive18_vs_static18 | SinceAlways.16b48k.wav | +0.000% |
| adaptive18_vs_static18 | SlavesOfFear.16b48k.wav | +0.000% |
| adaptive18_vs_static18 | The Chastising of Renegade.16b48k.wav | +0.000% |
| adaptive18_vs_static18 | Through The Fire And Flames.16b48k.wav | +0.000% |
| adaptive18_vs_static18 | TrosYGareg.16b48k.wav | +0.000% |
| adaptive18_vs_static18 | Waiting.16b48k.wav | +0.000% |
| adaptive18_vs_static18 | With Love (Outro).16b48k.wav | +0.000% |
| adaptive18_vs_static18 | bah.wav | +0.000% |
| adaptive18_vs_static18 | bas.wav | +0.000% |
| adaptive18_vs_static18 | bonhemian_rhapsody.16b48k.wav | +0.000% |
| adaptive18_vs_static18 | clapton_44k.16b48k.wav | +0.000% |
| adaptive18_vs_static18 | cst.wav | +0.000% |
| adaptive18_vs_static18 | fms.wav | +0.000% |
| adaptive18_vs_static18 | girl.16b48k.wav | +0.388% |
| adaptive18_vs_static18 | glk.wav | +0.239% |
| adaptive18_vs_static18 | hrp.wav | +1.416% |
| adaptive18_vs_static18 | liberate.16b48k.wav | +0.000% |
| adaptive18_vs_static18 | lob.wav | +0.000% |
| adaptive18_vs_static18 | mof.wav | +0.000% |
| adaptive18_vs_static18 | qrt.wav | +0.000% |
| adaptive18_vs_static18 | sandman.16b48k.wav | -0.102% |
| adaptive18_vs_static18 | take_your_finger_frin_my_head.16b48k.wav | -0.093% |
| adaptive18_vs_static18 | trumpet.16b48k.wav | +0.241% |
| adaptive18_vs_static18 | velvet.16b48k.wav | +0.000% |
| adaptive18_vs_static24 | 12-German-male-speech.441.16b48k.wav | +0.112% |
| adaptive18_vs_static24 | 15-Good-evening.441.16b48k.wav | +0.003% |
| adaptive18_vs_static24 | 21-classic.441.16b48k.wav | +0.007% |
| adaptive18_vs_static24 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav | +0.004% |
| adaptive18_vs_static24 | 25-This-is-the-end.441.16b48k.wav | +0.003% |
| adaptive18_vs_static24 | 27-last-song-drums-and-trampets.441.16b48k.wav | -0.017% |
| adaptive18_vs_static24 | 35_SQAM_glockenspiel_cut.16b48k.wav | +0.636% |
| adaptive18_vs_static24 | 4-Sound-English-male.441.16b48k.wav | -0.000% |
| adaptive18_vs_static24 | 9-Have-big-expensive-car.441.16b48k.wav | +0.020% |
| adaptive18_vs_static24 | Asleep__4.11-4.31_.16b48k.wav | +0.014% |
| adaptive18_vs_static24 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav | +0.113% |
| adaptive18_vs_static24 | Changes.16b48k.wav | +0.023% |
| adaptive18_vs_static24 | Coral.16b48k.wav | +0.026% |
| adaptive18_vs_static24 | ExitMusic.16b48k.wav | -0.005% |
| adaptive18_vs_static24 | Girl_In_The_Fire__Sample_.16b48k.wav | +0.005% |
| adaptive18_vs_static24 | Greatest_Love_of_All_2min57.16b48k.wav | +0.002% |
| adaptive18_vs_static24 | Hey Tonight.16b48k.wav | -0.008% |
| adaptive18_vs_static24 | Hotel California.16b48k.wav | +0.000% |
| adaptive18_vs_static24 | Jupiter, the Bringer of Jolity.16b48k.wav | +0.032% |
| adaptive18_vs_static24 | Last_Of_The_Mohicanz__Sample_.16b48k.wav | +0.795% |
| adaptive18_vs_static24 | NewYorkCity.16b48k.wav | +0.002% |
| adaptive18_vs_static24 | Only Time.16b48k.wav | -0.010% |
| adaptive18_vs_static24 | Robots_old.16b48k.wav | +0.024% |
| adaptive18_vs_static24 | Severance__1.31-1.51_.16b48k.wav | +0.012% |
| adaptive18_vs_static24 | Shinsho_pool_3min45_4min4.16b48k.wav | +0.062% |
| adaptive18_vs_static24 | SinceAlways.16b48k.wav | -0.004% |
| adaptive18_vs_static24 | SlavesOfFear.16b48k.wav | +0.002% |
| adaptive18_vs_static24 | The Chastising of Renegade.16b48k.wav | -0.029% |
| adaptive18_vs_static24 | Through The Fire And Flames.16b48k.wav | -0.003% |
| adaptive18_vs_static24 | TrosYGareg.16b48k.wav | +0.006% |
| adaptive18_vs_static24 | Waiting.16b48k.wav | +0.008% |
| adaptive18_vs_static24 | With Love (Outro).16b48k.wav | +0.004% |
| adaptive18_vs_static24 | bah.wav | +0.068% |
| adaptive18_vs_static24 | bas.wav | -0.003% |
| adaptive18_vs_static24 | bonhemian_rhapsody.16b48k.wav | +0.063% |
| adaptive18_vs_static24 | clapton_44k.16b48k.wav | -0.013% |
| adaptive18_vs_static24 | cst.wav | -0.049% |
| adaptive18_vs_static24 | fms.wav | +0.011% |
| adaptive18_vs_static24 | girl.16b48k.wav | +0.384% |
| adaptive18_vs_static24 | glk.wav | +0.504% |
| adaptive18_vs_static24 | hrp.wav | +1.357% |
| adaptive18_vs_static24 | liberate.16b48k.wav | +0.010% |
| adaptive18_vs_static24 | lob.wav | -0.020% |
| adaptive18_vs_static24 | mof.wav | -0.063% |
| adaptive18_vs_static24 | qrt.wav | -0.024% |
| adaptive18_vs_static24 | sandman.16b48k.wav | -0.090% |
| adaptive18_vs_static24 | take_your_finger_frin_my_head.16b48k.wav | -0.075% |
| adaptive18_vs_static24 | trumpet.16b48k.wav | +0.263% |
| adaptive18_vs_static24 | velvet.16b48k.wav | +0.044% |

Packet and decoded PCM identity counts exclude MP4/container metadata. This report describes benchmark results and does not establish a general optimum.
