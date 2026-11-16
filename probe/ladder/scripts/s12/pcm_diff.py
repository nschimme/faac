#!/usr/bin/env python3
"""Why do the 77 local clips not match manifest src_sha256? Header structure, length, resampling, SNR vs Apple LC 320k decode."""
import json, os, struct, subprocess, sys, tempfile, wave
import numpy as np
import soundfile as sf
sys.path.insert(0, '/home/user/work/fb')
os.chdir('/home/user/work/fb')
import setup_datasets as sd

REF = '/home/user/faac/probe/ladder/ref'
AUD = '/home/user/work/fb/data/external/audio'
OUT = '/home/user/faac/probe/ladder/results/s12-apple48k/stage0'

def chunks(b):
    out, i = [], 12
    while i + 8 <= len(b):
        cid = b[i:i+4].decode('latin1'); sz = struct.unpack('<I', b[i+4:i+8])[0]
        out.append((cid, sz, i))
        i += 8 + sz + (sz & 1)
    return out

def header(path):
    b = open(path, 'rb').read()
    ch = chunks(b)
    isft = None
    for cid, sz, off in ch:
        if cid == 'LIST':
            isft = b[off+8:off+8+sz]
    data = [c for c in ch if c[0] == 'data'][0]
    return dict(riff_size=struct.unpack('<I', b[4:8])[0], file_size=len(b), chunks=[(c, s) for c, s, _ in ch],
                isft=isft.decode('latin1').replace('\0', '') if isft else None,
                data_size_ok=data[1] == len(b) - data[2] - 8)

def mono(x): return x.mean(axis=1)

def lag_snr(x, y, maxlag=8192, win=48000 * 5):
    """x local, y decode (both [n,2] float). Returns (lag, snr_db): y delayed by lag vs x."""
    m = min(len(x), len(y)); xm, ym = mono(x[:m]), mono(y[:m])
    n = min(win, m)
    c = np.concatenate(([0.0], np.cumsum(xm ** 2)))
    s = int(np.argmax(c[n:] - c[:m + 1 - n])) if m > n else 0
    a = xm[s:s+n]; 
    seg = ym[max(0, s-maxlag):s+n+maxlag]; off = s - max(0, s-maxlag)
    L = len(a) + len(seg)
    nf = 1 << (L - 1).bit_length()
    cc = np.fft.irfft(np.fft.rfft(seg, nf) * np.conj(np.fft.rfft(a, nf)), nf)
    # position p of a inside seg: cc[p]
    cand = np.r_[np.arange(0, len(seg) - len(a) + 1)]
    p = int(cand[np.argmax(cc[cand])]); lag = p - off  # y[k+lag] ~ x[k]
    if lag >= 0:
        xa, ya = x[:len(y) - lag], y[lag:]
    else:
        xa, ya = x[-lag:], y[:len(x) + lag]
    k = min(len(xa), len(ya)); xa, ya = xa[:k], ya[:k]
    err = np.sum((xa - ya) ** 2)
    return lag, float(10 * np.log10(np.sum(xa ** 2) / max(err, 1e-30)))

def main():
    if len(sys.argv) > 1 and sys.argv[1] == '--md':
        return write_md(json.load(open(OUT + '/pcm_diff.json')))
    ht = {r['clip']: r for r in json.load(open(OUT + '/hash_table.json'))['rows']}
    xm = json.load(open(REF + '/xaac-48k/manifest.json'))['rows']
    am = json.load(open(REF + '/apple-48k/manifest.json'))['rows']
    xdur = {}
    for r in xm: xdur.setdefault(r['clip'], set()).add(r['duration_s'])
    srcs = dict(sd.music_sources())
    rows = []
    for clip in sorted(ht):
        r = dict(clip=clip)
        p = f'{AUD}/{clip}'
        x, sr = sf.read(p, dtype='float64', always_2d=True)
        r['frames'] = len(x); r['header'] = header(p)
        # manifest-implied source length: xaac manifest duration_s (6 dp of source length; identical across rungs/profiles)
        ds = xdur.get(clip, set())
        r['xaac_duration_s'] = sorted(ds)[0] if len(ds) == 1 else sorted(ds)
        r['manifest_frames'] = round(sorted(ds)[0] * 48000) if len(ds) == 1 else None
        r['frame_diff_local_minus_manifest'] = r['frames'] - r['manifest_frames'] if r['manifest_frames'] else None
        r['length_match'] = r['frame_diff_local_minus_manifest'] == 0
        s = srcs.get(clip)
        r['source'] = os.path.relpath(s, 'data/temp') if s else None
        if s:
            dur, ch, rate = sd.source_info(s)
            info = sf.info(s)
            r.update(src_rate=rate, src_channels=ch, src_subtype=info.subtype, src_format=info.format)
            r['converted'] = rate != 48000 or ch != 2 or info.subtype != 'PCM_16'
            r['resampled'] = rate != 48000
        # SNR vs Apple LC 320k decode
        ap = f'{REF}/apple-48k/lc/320k/{clip}.m4a'
        with tempfile.TemporaryDirectory() as td:
            w = td + '/d.wav'
            subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', ap, '-sample_fmt', 's16', w], check=True)
            y, ysr = sf.read(w, dtype='float64', always_2d=True)
        r['apple320_decode_frames'] = len(y); r['apple320_rate'] = ysr
        r['lag'], r['snr_db'] = lag_snr(x, y)
        rows.append(r)
        print(clip, r['lag'], round(r['snr_db'], 2), r['frame_diff_local_minus_manifest'], r.get('resampled'), flush=True)
    json.dump(rows, open(OUT + '/pcm_diff.json', 'w'), indent=1)
    write_md(rows)

def write_md(rows):
    def grp(f): return [r for r in rows if f(r)]
    def st(g, k):
        v = np.array([r[k] for r in g], float)
        return f'n={len(v)} mean {v.mean():.2f} median {np.median(v):.2f} min {v.min():.2f} max {v.max():.2f}' if len(v) else 'n=0'
    L = ['# Why the 77 local clips do not match manifest src_sha256', '',
         'Manifest-implied source length = round(xaac-48k manifest `duration_s` * 48000) (xaac duration_s is constant across all rungs/profiles of a clip and equals the source length for the clips where it can be checked, e.g. 01 castanets 6.63425 s = 318444 frames). Apple `duration_s` includes encoder priming (2112) + padding, so it was only used as a +-1024 consistency check (77/77 consistent, see hash_table.md).',
         'SNR: local WAV vs ffmpeg decode of `apple-48k/lc/320k/<clip>.m4a`, lag found by FFT cross-correlation of the loudest 5 s mono window (+-8192 samples), SNR over the full aligned overlap. This is codec-noise-limited; it only discriminates gross content differences (resampler/content change would give a different distribution than clips known to be untouched copies).', '']
    L.append(f"Length match (local frames == manifest-implied): {sum(r['length_match'] for r in rows)}/77; diff distribution: " +
             str({d: sum(1 for r in rows if r['frame_diff_local_minus_manifest'] == d) for d in sorted({r['frame_diff_local_minus_manifest'] for r in rows}, key=lambda z: (z is None, z))}))
    L.append(f"Lags (samples): " + str({d: sum(1 for r in rows if r['lag'] == d) for d in sorted({r['lag'] for r in rows})}))
    nr = grp(lambda r: not r.get('converted')); cv = grp(lambda r: r.get('converted'))
    L += ['', f"- not converted (48 kHz stereo s16 source, passthrough): {st(nr, 'snr_db')}", f"- converted (rate/channels/bit-depth change): {st(cv, 'snr_db')}",
          f"- resampled (source rate != 48k): {st(grp(lambda r: r.get('resampled')), 'snr_db')}"]
    for name, g in (('not converted', nr), ('converted', cv)):
        L.append(f"- length match among {name}: {sum(r['length_match'] for r in g)}/{len(g)}")
    lay = sorted({str([(c if c != 'data' else 'data') for c, s_ in r['header']['chunks']]) for r in rows})
    L += ['', f"Header structure: chunk layouts {lay}; every ISFT ends with Lavf60.16.100 (local ffmpeg 6.1.1): {all(r['header']['isft'].endswith('Lavf60.16.100') for r in rows)}; "
          f"RIFF size == file size-8 for all: {all(r['header']['riff_size'] == r['header']['file_size'] - 8 for r in rows)}; data chunk size consistent with file: {all(r['header']['data_size_ok'] for r in rows)}; "
          f"{len({r['header']['isft'] for r in rows})} distinct LIST/INFO payloads (the LIST carries the SOURCE file's tags -- INAM/IART/ICMT/... -- plus ISFT, so header bytes depend on which tags ffmpeg's FLAC/WAV demuxer->wav muxer copies; rewriting only ISFT cannot reproduce the manifest hash for tagged clips; with 0/77 matches over ISFT Lavf60-65.x.100/101, the 52 clips whose LIST holds only ISFT also fail, incl. passthrough ones with matching length, so the header/ISFT string alone does not explain it: the manifest-side files must differ in other header bytes or in sample content).", '',
          'Interpretation: SNR vs the Apple 320k decode is 25-46 dB for every clip with lag 0 for all 77, i.e. content is the same audio everywhere; the distributions for passthrough and resampled clips overlap, so SNR cannot separate LSB/header differences from resampler differences (it is codec-noise-limited). Length: 41 clips are +1 frame vs the xaac-implied length (21 of them passthrough 48 kHz sources, where no resampler is involved), 36 equal.', '',
          'Per-clip verdict (cause is a hypothesis; original WAVs are not available):', '',
          '| clip | source | src rate/ch/fmt | frames local | frames manifest | len match | lag | SNR dB | likely cause |', '|---|---|---|---|---|---|---|---|---|']
    snr_nr = np.array([r['snr_db'] for r in nr]) if nr else np.array([0.0])
    med = float(np.median(snr_nr)); 
    for r in rows:
        if not r['length_match']:
            cause = 'length differs by %+d sample(s): %s' % (r['frame_diff_local_minus_manifest'], 'resampler length rounding (ffmpeg 6.1.1 vs 9.0.2 swr)' if r.get('resampled') else '1 frame on a passthrough 48 kHz clip: not the resampler; ffmpeg 6.1.1 vs 9.0.2 convert/trim path or xaac duration_s rounding (undeterminable without originals)')
        elif r.get('converted'):
            cause = 'length same; content may differ at LSB level (resampler/dither/filter version) and/or ISFT/header'
        else:
            cause = 'length same, passthrough source: header/ISFT-only difference most likely (content expected identical)'
        if abs(r['snr_db'] - med) > 6 and nr:
            cause += '; SNR %.1f dB far from passthrough median %.1f (codec-noise dependent, not conclusive)' % (r['snr_db'], med)
        L.append(f"| {r['clip']} | {r['source']} | {r.get('src_rate')}/{r.get('src_channels')}/{r.get('src_subtype')} | {r['frames']} | {r['manifest_frames']} | {'y' if r['length_match'] else 'n'} | {r['lag']} | {r['snr_db']:.2f} | {cause} |")
    open(OUT + '/pcm_diff.md', 'w').write('\n'.join(L) + '\n')

if __name__ == '__main__':
    main()
