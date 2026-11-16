# S12 stage0 sanity (faac master 3a656f7, LC, ABR `-b R`, 77 clips, mos only)

Rows: rows_sanity.jsonl (582 rows, 0 errors). Apple/fdk packaged arms NOT yet run (streams arrived after this run).

| rung | decoder | n | mean MOS | mean realized kbps |
|---|---|---|---|---|
| 128 | faad2 | 77 | 4.8668 | 126.36 |
| 128 | ffmpeg | 77 | 4.8666 | 126.36 |
| 128 | fdkdec | 77 | 4.8667 | 126.36 |
| 192 | faad2 | 77 | 4.9553 | 189.41 |
| 192 | ffmpeg | 77 | 4.9552 | 189.41 |
| 192 | fdkdec | 77 | 4.9552 | 189.41 |

- FAAC LC 192k mean MOS under faad2: 4.9553 (expected ~4.955): OK.
- Apple LC 128k mean realized kbps from manifest actual_kbps: 131.397 (expected ~131.4): OK. Apple 192k: 195.361 kbps. Apple 192k MOS (expect ~4.958) not scored (streams were absent at run time).
- Self-BD: faac2 (identical settings) vs faac on 10 clips at rungs 128,160,192 (3 rungs needed: bd_rate_curve requires >=3): 10/10 curves, every per-clip BD-rate exactly 0.0 (bd_selfcheck_faad2.md/json; "no valid curve: 67" are the clips not run for faac2).
