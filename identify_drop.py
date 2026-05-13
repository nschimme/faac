import numpy as np
import wave
import sys
import os

def load_mono(filename):
    if not os.path.exists(filename):
        raise FileNotFoundError(f"Input audio file not found: {filename}")
    with wave.open(filename, 'rb') as f:
        sr = f.getframerate()
        n_frames = f.getnframes()
        data = f.readframes(n_frames)
        sig = np.frombuffer(data, dtype=np.int16).reshape(-1, f.getnchannels())
        return sig.mean(axis=1).astype(np.float32), sr

def find_alignment_offset(ref, deg, sr):
    dur = len(ref) / sr
    if dur > 1.5:
        r_part = ref[sr//2 : 3*sr//2]
        offset_base = sr//2
    else:
        r_part = ref
        offset_base = 0
    d_part = deg[:min(len(deg), 3*sr)]
    if len(d_part) < len(r_part): return 0
    corr = np.correlate(d_part, r_part, mode='valid')
    return np.argmax(corr) - offset_base

def identify_drops(orig_file, dec_file):
    orig, sr_o = load_mono(orig_file)
    dec, sr_d = load_mono(dec_file)
    offset = find_alignment_offset(orig, dec, sr_o)
    win = 1024; hop = 512
    worst_ratio = 1.0; worst_time = 0
    for p_o in range(0, len(orig) - win, hop):
        p_d = p_o + offset
        if p_d < 0 or p_d + win > len(dec): continue
        rms_o = np.sqrt(np.mean(orig[p_o:p_o+win]**2))
        rms_d = np.sqrt(np.mean(dec[p_d:p_d+win]**2))
        if rms_o > 500:
            ratio = rms_d / rms_o
            if ratio < worst_ratio:
                worst_ratio = ratio
                worst_time = p_o / sr_o
    print(f"Worst energy ratio: {worst_ratio:.4f} at {worst_time:.3f}s")
    return worst_ratio

if __name__ == "__main__":
    identify_drops(sys.argv[1], sys.argv[2])
