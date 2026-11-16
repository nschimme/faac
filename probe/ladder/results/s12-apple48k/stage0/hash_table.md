# Corpus hash check

Manifest: `/home/user/faac/probe/ladder/ref/apple-48k/manifest.json` (corpus built with ffmpeg 9.0.2 (Homebrew)); local ffmpeg 6.1.1.

present 77/77; raw match 0; match after ISFT normalization 0; ISFT versions that matched: none

stream-local = (manifest Apple-stream duration_s - local duration) in samples @48k (not a source-length field; Apple adds priming/padding).

| clip | present | raw | norm | frames | dur s | rate/ch | RMS dBFS | stream-local smp | sha256 raw (16) | manifest (16) |
|---|---|---|---|---|---|---|---|---|---|---|
| 01 castanets.wav | y | n | n | 318444 | 6.6342 | 48000/2 | -22.67 |  | dd9035c8a6799174 | 7e52fa249dffad73 |
| 03 eig.wav | y | n | n | 720038 | 15.0008 | 48000/2 | -14.78 |  | f45d780ad93f61d6 | 9529dae9ce800e85 |
| 04 Bachpsichord.wav | y | n | n | 1200009 | 25.0002 | 48000/2 | -22.78 |  | 17640564fbe3e80f | 77877eabfab90a73 |
| 05 Enola.wav | y | n | n | 1358518 | 28.3025 | 48000/2 | -18.38 |  | e31fe28685fbd59a | 21c4335ef221c420 |
| 06 trumpet.wav | y | n | n | 481488 | 10.0310 | 48000/2 | -17.23 |  | dad32f1eaf888274 | e45e2040e8d042e7 |
| 07 applaud.wav | y | n | n | 396100 | 8.2521 | 48000/2 | -11.60 |  | df08fbfc4de74ceb | 1718e8756e002c50 |
| 08 velvet.wav | y | n | n | 570172 | 11.8786 | 48000/2 | -13.00 |  | 4ed08b39ad8e4f6c | 1f4e6d7f8baa8749 |
| 09 Linchpin.wav | y | n | n | 742642 | 15.4717 | 48000/2 | -12.78 |  | e8a40f668a8e3f13 | 18482ecf1d594b4e |
| 10 Angels_Fall_First_ringing.wav | y | n | n | 1445437 | 30.1133 | 48000/2 | -17.15 |  | a4644a1ecf975f8e | c92541bf3eb3fae0 |
| 10xh_.wav | y | n | n | 1440002 | 30.0000 | 48000/2 | -19.95 |  | 673265ee58454d29 | 397b160596c9cfa2 |
| 11 You look good to me.wav | y | n | n | 1058591 | 22.0540 | 48000/2 | -20.44 |  | f94a71a5f229694e | c16171408a7a74fa |
| 11ff_.wav | y | n | n | 1435611 | 29.9086 | 48000/2 | -26.27 |  | 9607eb72d297bd89 | 98276e74143930be |
| 12-German-male-speech.441.16b48k.wav | y | n | n | 376327 | 7.8401 | 48000/2 | -22.17 |  | 787c40000b866e2b | 1f59f9034f52c0e2 |
| 12at_.wav | y | n | n | 918929 | 19.1444 | 48000/2 | -30.38 |  | 1146905ef510164d | 01a1832f5cace0ba |
| 13 fatboy_30sec.wav | y | n | n | 1401917 | 29.2066 | 48000/2 | -19.07 |  | 9937772a5ef5c07e | 881cacd09ab17c9d |
| 13by_.wav | y | n | n | 1175712 | 24.4940 | 48000/2 | -11.38 |  | 304dc9f535c9e97b | 86340afa57ed090b |
| 14fe_.wav | y | n | n | 960213 | 20.0044 | 48000/2 | -13.89 |  | 749b7ebb8cf754e4 | e0f91377358b8778 |
| 15-Good-evening.441.16b48k.wav | y | n | n | 411158 | 8.5658 | 48000/2 | -18.20 |  | 0c6fa32b5eec7cc6 | 9de269849efe73eb |
| 15ma_.wav | y | n | n | 838466 | 17.4680 | 48000/2 | -18.31 |  | 274539580fa85c27 | af1f837b59283191 |
| 16mb_.wav | y | n | n | 978285 | 20.3809 | 48000/2 | -12.99 |  | 602d7ed3507e30f6 | 1a318ce1afd40a23 |
| 17qu_.wav | y | n | n | 1355078 | 28.2308 | 48000/2 | -16.07 |  | 3caa4798dd2623ff | c9863758f88903d3 |
| 18vr_.wav | y | n | n | 495835 | 10.3299 | 48000/2 | -19.37 |  | c1546450dc4fe871 | 2ef008ce9025a6d1 |
| 19am_.wav | y | n | n | 1204703 | 25.0980 | 48000/2 | -17.07 |  | 3d68f9b345992020 | 6efb775c762c6ab3 |
| 20tt_.wav | y | n | n | 1392909 | 29.0189 | 48000/2 | -21.54 |  | 10bb2056b098e704 | 4e5c52ebee6a2dd1 |
| 21-classic.441.16b48k.wav | y | n | n | 463494 | 9.6561 | 48000/2 | -15.53 |  | 54a91b0e95fc022c | 6839f85782a5fb9a |
| 21wa_.wav | y | n | n | 967437 | 20.1549 | 48000/2 | -13.03 |  | a899597f918eab72 | 51a6a624c0310a66 |
| 22ex_.wav | y | n | n | 960324 | 20.0068 | 48000/2 | -12.97 |  | cc321656b0539520 | 92abd7b431c230c5 |
| 23ht_.wav | y | n | n | 1363485 | 28.4059 | 48000/2 | -12.61 |  | 924b3c2f017e154e | 7e1bd90e7160d6d8 |
| 24-Greensleeves-Korean-male-speech.441.16b48k.wav | y | n | n | 418560 | 8.7200 | 48000/2 | -24.01 |  | 2e9c42df99bfb7be | 82e5ff42ff052c96 |
| 24td_.wav | y | n | n | 953200 | 19.8583 | 48000/2 | -23.09 |  | d9f48f3a5ed96da7 | 41b8094d30ff224b |
| 25-This-is-the-end.441.16b48k.wav | y | n | n | 440849 | 9.1844 | 48000/2 | -17.06 |  | 88f3fa98e2377876 | 79679fca0e85860d |
| 25rb_.wav | y | n | n | 1440000 | 30.0000 | 48000/2 | -24.14 |  | 464b0091f9ea6747 | 043cd601e867ece8 |
| 26fr_.wav | y | n | n | 1418058 | 29.5429 | 48000/2 | -22.04 |  | a440e6081f0378dd | 80aba10978064f86 |
| 27-last-song-drums-and-trampets.441.16b48k.wav | y | n | n | 478350 | 9.9656 | 48000/2 | -22.01 |  | 7ce6c67300ce6caf | e1e6ef991e39e8fd |
| 27ud_.wav | y | n | n | 1317639 | 27.4508 | 48000/2 | -11.24 |  | acb31151d9a0f60d | c853cdfba3d11358 |
| 28db_.wav | y | n | n | 954568 | 19.8868 | 48000/2 | -13.70 |  | 2f24464bb80d88fb | af8bdc7b3b43c0f2 |
| 29ru_.wav | y | n | n | 959662 | 19.9930 | 48000/2 | -10.48 |  | f895a85973754702 | e00af5fa1d0a3029 |
| 35_SQAM_glockenspiel_cut.16b48k.wav | y | n | n | 1125612 | 23.4503 | 48000/2 | -26.31 |  | 3f78932e87050325 | d29dea7c3972970b |
| 4-Sound-English-male.441.16b48k.wav | y | n | n | 433351 | 9.0281 | 48000/2 | -19.25 |  | 04102715468d66e5 | bb159a9329ddc4e7 |
| 9-Have-big-expensive-car.441.16b48k.wav | y | n | n | 465927 | 9.7068 | 48000/2 | -18.88 |  | 927d3b5fd3d9e240 | 207abf8e390c944d |
| Asleep__4.11-4.31_.16b48k.wav | y | n | n | 960000 | 20.0000 | 48000/2 | -11.34 |  | dac7339b0496263a | 2e5146c38bb15a02 |
| Berling Drug.wav | y | n | n | 628559 | 13.0950 | 48000/2 | -13.86 |  | 15b63f7052fbd4ff | 1e6a0d7a7331356d |
| Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav | y | n | n | 1436800 | 29.9333 | 48000/2 | -20.26 |  | cd00baa280e0cbf8 | c9cea1e533c57b17 |
| Changes.16b48k.wav | y | n | n | 1436722 | 29.9317 | 48000/2 | -25.43 |  | 9690041c9ecf5c07 | 6c767e6d223202c1 |
| Coral.16b48k.wav | y | n | n | 609651 | 12.7011 | 48000/2 | -16.17 |  | 0527f7a4ad689cc2 | 87244fcd612dcb91 |
| ExitMusic.16b48k.wav | y | n | n | 965091 | 20.1061 | 48000/2 | -19.98 |  | 97f98421890d4f9f | 891e7e2325e8c72f |
| Girl_In_The_Fire__Sample_.16b48k.wav | y | n | n | 659631 | 13.7423 | 48000/2 | -24.09 |  | 7f6df67bd9e92217 | 8feab6849e826146 |
| Greatest_Love_of_All_2min57.16b48k.wav | y | n | n | 968551 | 20.1781 | 48000/2 | -19.95 |  | 69ba56cb70ae4e3d | 091b341885cf802c |
| Hey Tonight.16b48k.wav | y | n | n | 960000 | 20.0000 | 48000/2 | -15.68 |  | 2374890e2e318b66 | 1016d9e93738fd9e |
| Hotel California.16b48k.wav | y | n | n | 1476584 | 30.7622 | 48000/2 | -17.41 |  | edf9fd12e1d16531 | 1e63738b494e0f19 |
| Jupiter, the Bringer of Jolity.16b48k.wav | y | n | n | 1403239 | 29.2341 | 48000/2 | -25.24 |  | f6f689b4e5e5e854 | 3c7d41b915b7f4ad |
| Last_Of_The_Mohicanz__Sample_.16b48k.wav | y | n | n | 1223097 | 25.4812 | 48000/2 | -11.29 |  | cb47f2f9dff59d92 | ee4fdb3ff3df590e |
| NewYorkCity.16b48k.wav | y | n | n | 1432763 | 29.8492 | 48000/2 | -13.01 |  | cf9dadaa3ed27cb6 | 75beafaec48d1258 |
| Only Time.16b48k.wav | y | n | n | 1184213 | 24.6711 | 48000/2 | -15.58 |  | 4b3ac6d71fb0d3cc | a992d4302bf1ab7c |
| Robots_old.16b48k.wav | y | n | n | 1440000 | 30.0000 | 48000/2 | -22.76 |  | 4f384753c69bc258 | 256bc6cd7b1db148 |
| Severance__1.31-1.51_.16b48k.wav | y | n | n | 960000 | 20.0000 | 48000/2 | -22.64 |  | ddb4c891c62d3894 | c2f2c770af3a254d |
| Shinsho_pool_3min45_4min4.16b48k.wav | y | n | n | 908361 | 18.9242 | 48000/2 | -23.41 |  | 944d7c77080df332 | 5fb6969f55fcf63f |
| SinceAlways.16b48k.wav | y | n | n | 873185 | 18.1914 | 48000/2 | -15.26 |  | c699650edd4c14dd | dc7c6760ceca7748 |
| SlavesOfFear.16b48k.wav | y | n | n | 1056000 | 22.0000 | 48000/2 | -11.62 |  | 658f9db265c395d9 | c8e84b230ad26ebd |
| The Chastising of Renegade.16b48k.wav | y | n | n | 960000 | 20.0000 | 48000/2 | -18.15 |  | 15039ddd4e2a38bb | af703d4acaea16ea |
| Through The Fire And Flames.16b48k.wav | y | n | n | 1429883 | 29.7892 | 48000/2 | -12.74 |  | 6f5bd37e83143f1f | 6463125fb9663f2c |
| TrosYGareg.16b48k.wav | y | n | n | 1070611 | 22.3044 | 48000/2 | -11.84 |  | 741998d6e2c01491 | 713a12058aa9a676 |
| With Love (Outro).16b48k.wav | y | n | n | 787350 | 16.4031 | 48000/2 | -16.37 |  | 99be2ce98ec9dc33 | 20b53703992156f8 |
| bah.wav | y | n | n | 455104 | 9.4813 | 48000/2 | -23.50 |  | eae7eccf5983ad6e | 4642be4c412cb6a7 |
| bas.wav | y | n | n | 572652 | 11.9302 | 48000/2 | -21.88 |  | ed8ce50632d59466 | 5186a402f6eaf718 |
| bonhemian_rhapsody.16b48k.wav | y | n | n | 1368238 | 28.5050 | 48000/2 | -30.83 |  | efc52e1839266024 | ce8d9c37dbc12761 |
| clapton_44k.16b48k.wav | y | n | n | 719349 | 14.9864 | 48000/2 | -23.58 |  | 63dcc2490c5efc81 | b8ef5e3e8b1dc0d3 |
| fms.wav | y | n | n | 532883 | 11.1017 | 48000/2 | -22.07 |  | c93c993887894f17 | f0940b28f83bdaba |
| girl.16b48k.wav | y | n | n | 765959 | 15.9575 | 48000/2 | -17.58 |  | 15b7c72aa7584bf4 | 690c6d88ff94be96 |
| glk.wav | y | n | n | 734094 | 15.2936 | 48000/2 | -26.99 |  | 13a0afd28af7cd20 | 3db63d15c2862bfa |
| hrp.wav | y | n | n | 422073 | 8.7932 | 48000/2 | -25.06 |  | 990b0accf42c454e | 70e7419720b972b4 |
| liberate.16b48k.wav | y | n | n | 240343 | 5.0071 | 48000/2 | -10.57 |  | bb861897fc6dc800 | 34206cff7eff1450 |
| lob.wav | y | n | n | 340427 | 7.0922 | 48000/2 | -24.86 |  | bff24530f94dc6be | 9e145d90b34a01fa |
| mof.wav | y | n | n | 511121 | 10.6484 | 48000/2 | -22.80 |  | e7e203dcb91babd9 | 4e416d1c5be6123e |
| qrt.wav | y | n | n | 589045 | 12.2718 | 48000/2 | -20.97 |  | e1e5016297189a7e | 19e44e7515ceed5a |
| sandman.16b48k.wav | y | n | n | 1440109 | 30.0023 | 48000/2 | -17.40 |  | 67113fa44626b4b8 | 23760ae39834adf4 |
| take_your_finger_frin_my_head.16b48k.wav | y | n | n | 1060419 | 22.0921 | 48000/2 | -17.06 |  | 73631a08e8726573 | ec4c1fa5374101fc |
