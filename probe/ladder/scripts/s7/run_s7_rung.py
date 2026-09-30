import os, sys, glob, subprocess, json, math, re, statistics as st
import concurrent.futures as cf

A = os.environ.get('FAAC_BENCHMARK_DATA', '/opt/faac-benchmark/data/external/audio')
REF_DIR = os.environ.get('REF_DIR', 'probe/ladder/ref')
SC = ['python3', '/opt/faac-benchmark/scripts/score_clip.py']

# Ensure PATH and LD_LIBRARY_PATH include /home/jules/fdk if built there
fdk_env = dict(os.environ, LD_LIBRARY_PATH=f"/home/jules/fdk/lib:{os.environ.get('LD_LIBRARY_PATH','')}", PATH=f"/home/jules/fdk/bin:{os.environ.get('PATH','')}")

def run_rung(rung_name, rate, lo, hi, faac_bin, is_lc_below72=False):
    work_dir = f"/tmp/s7_work_{rung_name}"
    os.makedirs(work_dir, exist_ok=True)
    rf_path = os.path.join(work_dir, "results.json")

    results = json.load(open(rf_path)) if os.path.exists(rf_path) else {}
    clips = sorted(glob.glob(os.path.join(A, '*.wav')))

    # 1. Encode all arms for all clips
    arms_config = [
        ('ctl', rate, is_lc_below72),
        (f's{lo}', str(lo), is_lc_below72),
        (f's{hi}', str(hi), is_lc_below72),
    ]

    # Encoder flags helper
    def get_faac_cmd(arm_rate, use_lc, out_path, in_wav):
        cmd = [faac_bin, '--overwrite', '-b', str(arm_rate)]
        if use_lc:
            cmd = [faac_bin, '--overwrite', '--object-type', 'lc', '-b', str(arm_rate)]
        cmd.extend(['-o', out_path, in_wav])
        return cmd

    print(f"[{rung_name}] Encoding FAAC arms...")
    for arm_name, arm_rate, use_lc in arms_config:
        def enc_job(c):
            st_name = os.path.basename(c)[:-4]
            o = os.path.join(work_dir, f"{arm_name}__{st_name}.m4a")
            if not os.path.exists(o):
                cmd = get_faac_cmd(arm_rate, use_lc, o, c)
                subprocess.run(cmd, capture_output=True, check=True)
            return st_name, o, c

        with cf.ThreadPoolExecutor(4) as ex:
            list(ex.map(enc_job, clips))

    # fdk arm encoding
    print(f"[{rung_name}] Encoding fdk arm...")
    fdk_bin = "/home/jules/fdk/bin/fdkaac" if os.path.exists("/home/jules/fdk/bin/fdkaac") else "fdkaac"
    def enc_fdk_job(c):
        st_name = os.path.basename(c)[:-4]
        o = os.path.join(work_dir, f"fdk__{st_name}.m4a")
        if not os.path.exists(o):
            if rung_name.startswith("he"):
                # fdk HE: -S -p 5 -b <N>000
                cmd = [fdk_bin, '-S', '-p', '5', '-b', f"{rate}000", '-o', o, c]
            else:
                # fdk LC: -p 2 -b <N>000
                cmd = [fdk_bin, '-p', '2', '-b', f"{rate}000", '-o', o, c]
            subprocess.run(cmd, capture_output=True, check=True, env=fdk_env)
        return st_name, o, c

    with cf.ThreadPoolExecutor(4) as ex:
        list(ex.map(enc_fdk_job, clips))

    # 2. Score all streams serially
    print(f"[{rung_name}] Scoring streams serially...")

    # Determine Apple ref subdir
    apple_subdirs = {
        'lc64': 'apple_lc64k',
        'lc96': 'apple_lc96k',
        'lc128': 'apple',
        'he32': 'apple_he32k',
        'he48': 'apple_he48k'
    }
    apple_dir = os.path.join(REF_DIR, apple_subdirs[rung_name])

    all_arms = ['ctl', f's{lo}', f's{hi}', 'fdk', 'apple']
    for arm in all_arms:
        results.setdefault(arm, {})
        for c in clips:
            st_name = os.path.basename(c)[:-4]
            if st_name in results[arm]:
                continue

            if arm == 'apple':
                # Locate apple ref clip
                ref_file = os.path.join(apple_dir, f"{st_name}.m4a")
                if not os.path.exists(ref_file):
                    # try wave / exact stem
                    matches = glob.glob(os.path.join(apple_dir, f"{st_name}*"))
                    if matches:
                        ref_file = matches[0]
                    else:
                        raise FileNotFoundError(f"Missing Apple reference for {st_name} in {apple_dir}")
                target_m4a = ref_file
                cleanup = False
            else:
                target_m4a = os.path.join(work_dir, f"{arm}__{st_name}.m4a")
                cleanup = True

            out = subprocess.run(SC + [c, target_m4a], capture_output=True, text=True, env=dict(os.environ, PATH='/opt/venv312/bin:'+os.environ['PATH'])).stdout
            m = re.search(r'MOS:\s*([0-9.]+)', out)
            if not m:
                raise RuntimeError(f"Scoring failed for {arm} {st_name}: {out}")
            mos = float(m.group(1))
            bytes_cnt = os.path.getsize(target_m4a)

            results[arm][st_name] = {'mos': mos, 'bytes': bytes_cnt}
            json.dump(results, open(rf_path, 'w'), indent=2)

            if cleanup and os.path.exists(target_m4a):
                os.remove(target_m4a)

    print(f"[{rung_name}] All scoring complete for 5 arms x 49 clips!")
    return rf_path

if __name__ == '__main__':
    rung = sys.argv[1]
    faac_bin = sys.argv[2]

    configs = {
        'lc64': (64, 56, 72, True),
        'lc96': (96, 80, 112, False),
        'lc128': (128, 112, 144, False),
        'he32': (32, 28, 40, False),
        'he48': (48, 40, 56, False)
    }
    rate, lo, hi, is_lc = configs[rung]
    run_rung(rung, rate, lo, hi, faac_bin, is_lc)
