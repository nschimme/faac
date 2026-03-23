import subprocess
import json
import os

def run_iteration(it_name, b32, b64):
    print(f"--- Iteration {it_name}: 32k->{b32}, 64k->{b64} ---")
    body = f"""
    const unsigned int nyquist = sampleRate / 2;
    unsigned int bw;
    if (!bitRate) return nyquist;
    if (bitRate <= 32000) {{
        bw = 4000 + (bitRate * {b32-4000} / 32000);
    }} else if (bitRate <= 64000) {{
        bw = {b32} + ((bitRate - 32000) * {b64-b32} / 32000);
    }} else {{
        bw = {b64} + ((bitRate - 64000) / 16);
        if (bw > 20000) bw = 20000;
    }}
    return (bw > nyquist) ? nyquist : bw;
"""
    with open('libfaac/util.c', 'r') as f:
        lines = f.readlines()

    new_lines = []
    skip = False
    for line in lines:
        if 'unsigned int CalcBandwidth' in line:
            new_lines.append(line)
            new_lines.append("{\n")
            new_lines.append(body)
            new_lines.append("}\n")
            skip = True
        elif skip:
            if line.startswith('}'):
                skip = False
        else:
            new_lines.append(line)

    with open('libfaac/util.c', 'w') as f:
        f.writelines(new_lines)

    subprocess.run(['meson', 'compile', '-C', 'build'], check=True, capture_output=True)

    out = f"{it_name}.json"
    subprocess.run([
        'python3', '/opt/faac-benchmark/run_benchmark.py',
        './build/frontend/faac', './build/libfaac/libfaac.so',
        it_name, out, '--coverage', '30', '--scenarios', 'music_low,music_std'
    ], check=True, capture_output=True)

    with open(out, 'r') as f:
        data = json.load(f)
    sc = {}
    for v in data['matrix'].values():
        if v.get('mos'):
            sc.setdefault(v['scenario'], []).append(v['mos'])
    res = {s: round(sum(l)/len(l), 4) for s, l in sc.items()}
    print(f"Result: {res}")
    return res

# it37: Small increase at 32k (9000), small decrease at 64k (17000)
run_iteration('it37', 9000, 17000)
# it38: Original 32k (8000), decrease at 64k (16000)
run_iteration('it38', 8000, 16000)
# it39: increase both slightly
run_iteration('it39', 9000, 19000)
# it40: decrease both
run_iteration('it40', 7000, 15000)
