import json, os
def get_avg_mos(filepath):
    if not os.path.exists(filepath): return None
    with open(filepath, 'r') as f: data = json.load(f)
    mos_values = [v['mos'] for v in data['matrix'].values() if v['mos'] is not None and v['mos'] > 1.05]
    return sum(mos_values) / len(mos_values) if mos_values else 0.0
scenarios = ['music_low', 'voip', 'vss', 'music_std']
print(f"{'Scenario':<15} | {'Baseline':<10} | {'Candidate':<10} | {'Delta':<10}")
print("-" * 55)
for s in scenarios:
    b = get_avg_mos(f"benchmarks/baseline/{s}.json")
    c = get_avg_mos(f"benchmarks/candidate/{s}.json")
    if b is not None and c is not None: print(f"{s:<15} | {b:10.4f} | {c:10.4f} | {c-b:+10.4f}")
    else: print(f"{s:<15} | {b if b is not None else 'N/A':<10} | {c if c is not None else 'N/A':<10} | {'N/A':<10}")
