import subprocess
import time
import os

def run_bench(mode):
    # Edit frame.c to force a specific mode
    with open('libfaac/frame.c', 'r') as f:
        lines = f.readlines()

    new_lines = []
    for line in lines:
        if 'hEncoder->quantize_sfb_p = ' in line and 'quantize_sfb;' not in line:
            if mode == 'scalar':
                new_lines.append('    hEncoder->quantize_sfb_p = quantize_sfb;\n')
            elif mode == 'sse2':
                new_lines.append('    hEncoder->quantize_sfb_p = quantize_sfb_sse2;\n')
            elif mode == 'avx2':
                new_lines.append('    hEncoder->quantize_sfb_p = quantize_sfb_avx2;\n')
        elif 'cpu_caps_t caps = get_cpu_caps();' in line:
             new_lines.append('    /* cpu_caps_t caps = get_cpu_caps();\n')
        elif '#endif' in line and 'hEncoder->quantize_sfb_p = quantize_sfb_neon;' in line:
             new_lines.append('#endif */\n')
        else:
            new_lines.append(line)

    with open('libfaac/frame.c', 'w') as f:
        f.writelines(new_lines)

    subprocess.run(['ninja', '-C', 'build'], check=True, capture_output=True)

    start = time.time()
    subprocess.run(['python3', 'benchmark.py'], check=True, capture_output=True)
    return time.time() - start

modes = ['scalar', 'sse2', 'avx2']
results = {}
for m in modes:
    print(f"Benchmarking {m}...")
    results[m] = run_bench(m)

print("\nResults:")
for m, t in results.items():
    print(f"{m}: {t:.3f}s")
