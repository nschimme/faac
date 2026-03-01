import os
import time
import subprocess
import hashlib

def get_md5(filename):
    hash_md5 = hashlib.md5()
    with open(filename, "rb") as f:
        for chunk in iter(lambda: f.read(4096), b""):
            hash_md5.update(chunk)
    return hash_md5.hexdigest()

def run_benchmark(faac_bin, input_file):
    output_file = input_file.replace('.wav', '.aac')
    start_time = time.time()
    # -r for raw AAC (easier to compare)
    result = subprocess.run([faac_bin, '-r', '-o', output_file, input_file],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    end_time = time.time()

    if result.returncode != 0:
        return None, None

    duration = end_time - start_time
    md5 = get_md5(output_file)
    return duration, md5

if __name__ == '__main__':
    faac_bin = './build/frontend/faac'
    baselines = ['sine.wav', 'noise.wav', 'sweep.wav', 'silent.wav']

    print(f"{'Input':<15} | {'Time (s)':<10} | {'MD5 Hash'}")
    print("-" * 60)

    for b in baselines:
        path = os.path.join('baselines', b)
        if not os.path.exists(path):
            continue

        duration, md5 = run_benchmark(faac_bin, path)
        if duration is not None:
            print(f"{b:<15} | {duration:10.3f} | {md5}")
        else:
            print(f"{b:<15} | {'FAILED':<10} | -")
