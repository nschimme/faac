import subprocess
import json
import os

def run_iteration(it_name, bw_code):
    print(f"--- Starting Iteration: {it_name} ---")
    with open('libfaac/util.c', 'r') as f:
        content = f.read()

    start_marker = "unsigned int CalcBandwidth(unsigned long bitRate, unsigned long sampleRate)\n{"
    end_marker = "    /* Safety clamp to Shannon-Nyquist limit */"

    start_idx = content.find(start_marker)
    end_idx = content.find(end_marker)

    new_content = content[:start_idx + len(start_marker)] + bw_code + content[end_idx:]

    with open('libfaac/util.c', 'w') as f:
        f.write(new_content)

    subprocess.run(['meson', 'compile', '-C', 'build'], check=True, capture_output=True)

    output_file = f"{it_name}.json"
    subprocess.run([
        'python3', '/opt/faac-benchmark/run_benchmark.py',
        './build/frontend/faac', './build/libfaac/libfaac.so',
        it_name, output_file,
        '--coverage', '30',
        '--scenarios', 'music_low,music_std'
    ], check=True, capture_output=True)

    with open(output_file, 'r') as f:
        data = json.load(f)

    scenarios = {}
    for key, val in data['matrix'].items():
        sc = val['scenario']
        mos = val.get('mos')
        if mos is None: continue
        if sc not in scenarios:
            scenarios[sc] = []
        scenarios[sc].append(mos)

    res = {s: round(sum(l)/len(l), 4) for s, l in scenarios.items()}
    print(f"Results for {it_name}: {res}")
    return res

# it31: Increase bandwidth for music_low (32k/ch) and music_std (64k/ch)
# Legacy: 32k -> 8000, 64k -> 18000
# it31: 32k -> 10000, 64k -> 19000
run_iteration('it31', """
    const unsigned int nyquist = sampleRate / 2;
    unsigned int bw;
    if (!bitRate) return nyquist;
    if (bitRate <= 32000) {
        bw = 4000 + (bitRate * 6000 / 32000); // 32k -> 10000
    } else if (bitRate <= 64000) {
        bw = 10000 + ((bitRate - 32000) * 9000 / 32000); // 64k -> 19000
    } else {
        bw = 19000 + ((bitRate - 64000) / 16);
        if (bw > 20000) bw = 20000;
    }
""")

# it32: even more for music_low
# it32: 32k -> 12000, 64k -> 18000
run_iteration('it32', """
    const unsigned int nyquist = sampleRate / 2;
    unsigned int bw;
    if (!bitRate) return nyquist;
    if (bitRate <= 32000) {
        bw = 4000 + (bitRate * 8000 / 32000); // 32k -> 12000
    } else if (bitRate <= 64000) {
        bw = 12000 + ((bitRate - 32000) * 6000 / 32000); // 64k -> 18000
    } else {
        bw = 18000 + ((bitRate - 64000) / 16);
        if (bw > 20000) bw = 20000;
    }
""")
