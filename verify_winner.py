import os, subprocess, json, re

# Apply finalized code with winner params: d=1.14, c=0.2
d, c = 1.14, 0.2

with open('libfaac/frame.c', 'r') as f: content = f.read()
content = re.sub(r'pe_target = \(faac_real\)desbits / [\d\.]+;', f'pe_target = (faac_real)desbits / {d:.4f};', content)
content = re.sub(r'fix = \(fix - 1\.0\) \* [\d\.]+ \+ 1\.0; /\* PE loop convergence \*/', f'fix = (fix - 1.0) * {c:.4f} + 1.0; /* PE loop convergence */', content)
with open('libfaac/frame.c', 'w') as f: f.write(content)

subprocess.run(['ninja', '-C', 'build'], check=True)

# Run verification at 30% coverage
print("Running final verification at 30% coverage...")
out = 'final_verification.json'
subprocess.run(['python3', '/opt/faac-benchmark/run_benchmark.py', './build/frontend/faac', './build/libfaac/libfaac.so', 'final', out, '--coverage', '30'], check=True)

# Compare vs baseline_orig (if I had a 30% one, but I'll use what I have)
# Actually I'll just check if it produces reasonable MOS and doesn't crash.
with open(out, 'r') as f: data = json.load(f)
matrix = data['matrix']
mos_vals = [v['mos'] for v in matrix.values() if v['mos'] is not None]
print(f"Final Verification Complete. Samples: {len(matrix)}, Valid MOS: {len(mos_vals)}")
if mos_vals:
    print(f"Avg MOS: {sum(mos_vals)/len(mos_vals):.4f}")
