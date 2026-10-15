# Scorer audit

The original `score_combo_fixed8.py` is preserved with its `score.log` and raw `validation.json`. Its non-identity branch scored `reference.decoded_path` against the candidate decode, while the stored reference MOS was scored against the original input WAV. Those MOS values therefore had different anchors, invalidating the paired MOS deltas and BD-rate calculation. The observed run stopped at `21-classic.441.16b48k.wav` with “BD-rate invalid”; that failure is not a codec result.

`score_combo_fixed8_v2.py` is an isolated corrected version. It converts each original source WAV to 48 kHz stereo using the benchmark's normal `wav_conv` helper, then scores that against the candidate FDK PCM with Zimtohrli audio mode. It only reuses reference MOS when candidate and reference packet and decoded PCM hashes are identical. Its output uses `*-v2-*` names and leaves every raw validation artifact intact.
