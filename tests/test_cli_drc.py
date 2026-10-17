#!/usr/bin/env python3
#
# FAAC CLI DRC Integration and Regression Test Suite
#

import hashlib
import math
import os
import shutil
import struct
import subprocess
import sys
import wave

# Exit code for skipped tests in Meson
SKIP_EXIT_CODE = 77

# Baseline MD5 hashes captured on base branch (commit 958285041905156a6eea57d3bd7b1d5a04717e60)
# for synthetic 8s 48 kHz stereo WAV input (4s -40 dBFS 997 Hz sine, 4s -3 dBFS 997 Hz sine).
BASELINE_MD5 = {
    'lc_adts': '406df6509b0bbba06c9977e77854d19a',
    'lc_m4a':  '0c11e3cef5acd189d55bc72bffa1b70a',
    'he_adts': '92e36995b9f1231e18cd8af84de1e5ff',
    'he_m4a':  'cd1f9a8fb7eaafd440435827004bd5bf',
}

def generate_synthetic_wav(filename):
    rate = 48000
    secs = 8
    w = wave.open(filename, 'wb')
    w.setnchannels(2)
    w.setsampwidth(2)
    w.setframerate(rate)

    quiet_amp = 32767.0 * math.pow(10.0, -40.0 / 20.0)
    loud_amp = 32767.0 * math.pow(10.0, -3.0 / 20.0)

    out = bytearray()
    for i in range(rate * secs):
        amp = quiet_amp if i < rate * 4 else loud_amp
        t = i / rate
        val = int(amp * math.sin(2.0 * math.pi * 997.0 * t))
        out += struct.pack('<hh', val, val)

    w.writeframes(bytes(out))
    w.close()

def parse_adts_drc(buf):
    idx = 0
    frames = []
    while idx <= len(buf) - 7:
        if buf[idx] == 0xff and (buf[idx+1] & 0xf0) == 0xf0:
            frame_len = ((buf[idx+3] & 0x03) << 11) | (buf[idx+4] << 3) | ((buf[idx+5] & 0xe0) >> 5)
            if frame_len == 0 or idx + frame_len > len(buf):
                break
            b_idx = idx + 7
            id_fil = (buf[b_idx] >> 5) & 7
            if id_fil == 6:
                cnt = (buf[b_idx] >> 1) & 0xF
                ext_type = ((buf[b_idx] & 1) << 3) | ((buf[b_idx+1] >> 5) & 7)
                if ext_type == 0xB:
                    ref_pres = (buf[b_idx+1] >> 0) & 1
                    if ref_pres:
                        sgn = buf[b_idx+2] & 1
                        ctl = (buf[b_idx+3] >> 1) & 0x7F
                    else:
                        sgn = buf[b_idx+1] & 1
                        ctl = (buf[b_idx+2] >> 1) & 0x7F
                    gain_db = (-0.25 if sgn else 0.25) * ctl
                    frames.append(gain_db)
            idx += frame_len
        else:
            idx += 1
    return frames

def demux_m4a_to_adts_buf(m4a_path):
    res = subprocess.run(['ffmpeg', '-i', m4a_path, '-c:a', 'copy', '-f', 'adts', '-'],
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if res.returncode != 0:
        raise RuntimeError(f'ffmpeg demux failed: {res.stderr.decode()}')
    return res.stdout

def main():
    if len(sys.argv) < 2:
        print('Usage: test_cli_drc.py <path_to_faac_exe>')
        sys.exit(1)

    faac_exe = sys.argv[1]
    ffmpeg_exe = shutil.which('ffmpeg')

    wav_file = 'temp_synth.wav'
    generate_synthetic_wav(wav_file)

    try:
        # Test 1: Identity / Baseline test (no --drc flag)
        print('Test 1: Verification of no-flag MD5 identity against base branch constants...')
        cases = [
            ('lc_adts', ['-o', 'out_lc.adts', wav_file]),
            ('lc_m4a',  ['--no-encoder-info', '-o', 'out_lc.m4a', wav_file]),
            ('he_adts', ['--object-type', 'he-aac-v1', '-b', '32', '-o', 'out_he.adts', wav_file]),
            ('he_m4a',  ['--no-encoder-info', '--object-type', 'he-aac-v1', '-b', '32', '-o', 'out_he.m4a', wav_file]),
        ]

        for key, args in cases:
            outfile = args[args.index('-o') + 1]
            if os.path.exists(outfile):
                os.remove(outfile)
            res = subprocess.run([faac_exe] + args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            assert res.returncode == 0, f'Encoding failed for {key}: {res.stderr.decode()}'
            data = open(outfile, 'rb').read()
            md5 = hashlib.md5(data).hexdigest()
            assert md5 == BASELINE_MD5[key], f'MD5 mismatch for {key}: expected {BASELINE_MD5[key]}, got {md5}'
            os.remove(outfile)
        print('  -> Passed baseline MD5 identity test.')

        # Test 2: Error cases
        print('Test 2: Validation error cases...')
        # (a) --drc-ref-level without --drc
        res = subprocess.run([faac_exe, '--drc-ref-level', '-20', wav_file], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        assert res.returncode != 0 and b'--drc-ref-level requires --drc' in res.stderr
        # (b) Out of range / NaN ref levels
        res = subprocess.run([faac_exe, '--drc', '--drc-ref-level', '5.0', wav_file], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        assert res.returncode != 0 and b'Invalid --drc-ref-level' in res.stderr
        res = subprocess.run([faac_exe, '--drc', '--drc-ref-level', 'nan', wav_file], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        assert res.returncode != 0 and b'Invalid --drc-ref-level' in res.stderr
        print('  -> Passed error cases.')

        # Test 3: Functional --drc execution across LC and HE-AAC v1 (ADTS & M4A)
        print('Test 3: Functional --drc analysis and bitstream fill element verification...')
        drc_cases = [
            ('lc_adts', [faac_exe, '--drc', '-o', 'drc_lc.adts', wav_file], 'drc_lc.adts', 376, 376),
            ('lc_m4a',  [faac_exe, '--drc', '-o', 'drc_lc.m4a', wav_file], 'drc_lc.m4a', 376, 376),
            ('he_adts', [faac_exe, '--drc', '--object-type', 'he-aac-v1', '-b', '32', '-o', 'drc_he.adts', wav_file], 'drc_he.adts', 189, 190),
            ('he_m4a',  [faac_exe, '--drc', '--object-type', 'he-aac-v1', '-b', '32', '-o', 'drc_he.m4a', wav_file], 'drc_he.m4a', 189, 190),
        ]

        for name, cmd, outfile, min_frames, max_frames in drc_cases:
            if os.path.exists(outfile):
                os.remove(outfile)
            res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            assert res.returncode == 0, f'DRC encode failed for {name}: {res.stderr.decode()}'

            if outfile.endswith('.adts'):
                adts_bytes = open(outfile, 'rb').read()
            else:
                if not ffmpeg_exe:
                    print(f'Skipping M4A DRC parsing for {name} (ffmpeg not installed)')
                    continue
                adts_bytes = demux_m4a_to_adts_buf(outfile)

            gains = parse_adts_drc(adts_bytes)
            assert min_frames <= len(gains) <= max_frames, f'{name}: expected {min_frames}..{max_frames} DRC frames, got {len(gains)}'

            # Verify quiet boost (~+6 dB), loud cut (~-10 dB), max cut step <= 3.0 dB
            quiet_idx = int(len(gains) * 0.25)
            loud_idx = int(len(gains) * 0.85)
            boost_frame = gains[quiet_idx]
            loud_frame = gains[loud_idx]
            assert 5.0 <= boost_frame <= 6.0, f'{name}: boost frame gain {boost_frame} at idx {quiet_idx} not in [5.0, 6.0]'
            assert -10.5 <= loud_frame <= -9.5, f'{name}: loud frame gain {loud_frame} at idx {loud_idx} not in [-10.5, -9.5]'

            for i in range(1, len(gains)):
                cut_step = gains[i-1] - gains[i]
                if cut_step > 0.0:
                    assert cut_step <= 3.001, f'{name}: cut step {cut_step} exceeds 3.0 dB at frame {i}'

            os.remove(outfile)
        print('  -> Passed functional DRC tests.')

        # Test 4: Decoding test with ffmpeg
        if not ffmpeg_exe:
            print('ffmpeg not found; skipping ffmpeg decode verification test (exit code 77).')
            sys.exit(SKIP_EXIT_CODE)

        print('Test 4: Decoding verification with ffmpeg...')
        for name, cmd, outfile, _, _ in drc_cases:
            res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            assert res.returncode == 0
            dec_res = subprocess.run([ffmpeg_exe, '-v', 'error', '-i', outfile, '-f', 'null', '-'],
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            assert dec_res.returncode == 0, f'ffmpeg failed to decode {outfile}: {dec_res.stderr.decode()}'
            assert len(dec_res.stderr) == 0, f'ffmpeg reported errors decoding {outfile}: {dec_res.stderr.decode()}'
            os.remove(outfile)
        print('  -> Passed clean ffmpeg decode test.')

    finally:
        if os.path.exists(wav_file):
            os.remove(wav_file)

    print('All CLI DRC tests passed successfully.')

if __name__ == '__main__':
    main()
