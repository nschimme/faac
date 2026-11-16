# S12 anomaly prereg: FAAC HE-AAC v2 collapse at 48 kHz high rates (diagnostic only, no fix)

Gist (benchmark 9f5f102, FAAC 3.0.0 a7d2f755): HE-v2 48 kHz stereo mean MOS 3.848 at 128k, 3.822 at 160k, 3.634 at
192k, 2.712 at 256k, 2.207 at 320k; fdkaac flat at 3.775. Outlier rows: mof.wav (320k 1.59) and 18vr_.wav (256k 1.87).

Questions: does it reproduce on master; is it an encoder defect (bitstream/decoder-independent) or a scorer/decoder
artefact; what in the stream changes between 128k and 256k.

Plan (fixed before running):
1. Clips: mof, 18vr_ (gist outliers) plus 4 more: the 2 clips with the largest 160k->256k drop among the first 20
   clips encoded, and 2 random clips (seed 0). Rates 96/128/160/192/256/320, `--object-type` HE-v2 as the benchmark.
2. Per stream: realized kbps, decode under FAAD2, FFmpeg, fdkdec (exit status, warnings, output rate/channels/length),
   MOS per decoder, decision dump (core window/sf/codebook ranges, global_gain range, PS/SBR fields).
3. Call it an encoder defect if MOS drops by > 0.5 from 160k to 256k on a clip under all three decoders, or any
   decoder reports a bitstream error. Call it decoder-specific if one decoder drops and the others do not.
4. Report the mechanism if the dumps show one (e.g. a core rate/bandwidth clamp, a field out of range). Any fix gets its
   own prereg; this item stops at the diagnosis.
