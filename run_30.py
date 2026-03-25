import os, subprocess, json, re

def eval_point(d, c):
    with open('libfaac/frame.c', 'r') as f: content = f.read()
    content = re.sub(r'pe_target = \(faac_real\)desbits / [\d\.]+;', f'pe_target = (faac_real)desbits / {d:.4f};', content)
    content = re.sub(r'fix = \(fix - 1\.0\) \* [\d\.]+ \+ 1\.0; /\* PE loop convergence \*/', f'fix = (fix - 1.0) * {c:.4f} + 1.0; /* PE loop convergence */', content)
    with open('libfaac/frame.c', 'w') as f: f.write(content)
    subprocess.run(['ninja', '-C', 'build'], check=True, capture_output=True)
    out = 'temp.json'
    subprocess.run(['python3', '/opt/faac-benchmark/run_benchmark.py', './build/frontend/faac', './build/libfaac/libfaac.so', 'temp', out, '--coverage', '5'], check=True, capture_output=True)
    with open('baseline_orig.json', 'r') as f: old = json.load(f)['matrix']
    with open(out, 'r') as f: cand = json.load(f)['matrix']
    deltas = [cand[k]['mos'] - old[k]['mos'] for k in cand if k in old and cand[k]['mos'] is not None and old[k]['mos'] is not None]
    avg = sum(deltas)/len(deltas) if deltas else 0
    reg = len([d for d in deltas if d < -0.05])
    win = len([d for d in deltas if d > 0.05])
    return {'d': d, 'c': c, 'avg': avg, 'reg': reg, 'win': win}

divisors = [1.10, 1.14, 1.18, 1.22, 1.26]
convergences = [0.05, 0.1, 0.2, 0.3, 0.4, 0.5]
results = []
for d in divisors:
    for c in convergences:
        try:
            res = eval_point(d, c)
            print(f"RESULT: {res}")
            results.append(res)
        except: pass

results.sort(key=lambda x: x['avg'], reverse=True)
with open('summary_30.json', 'w') as f: json.dump(results, f, indent=2)
if results:
    w = results[0]
    print(f"\nWINNER: d={w['d']} c={w['c']} Avg={w['avg']:.4f}")
