import os, json, math, statistics as st

rungs = [
    ('lc64', '56', '72'),
    ('lc96', '80', '112'),
    ('lc128', '112', '144'),
    ('he32', '28', '40'),
    ('he48', '40', '56')
]

s5_s6_baseline = {
    ('apple', 'lc64'): "-0.48",
    ('apple', 'lc96'): "-0.003",
    ('apple', 'lc128'): "+0.008",
    ('apple', 'he32'): "-0.011",
    ('apple', 'he48'): "+0.042..+0.058",
    ('fdk', 'lc64'): "-0.203",
    ('fdk', 'lc96'): "+0.000",
    ('fdk', 'lc128'): "-0.021",
    ('fdk', 'he32'): "+0.015",
    ('fdk', 'he48'): "+0.075"
}

def analyze():
    print("====================================================================================================")
    print("STAGE S7-STACK SCOREBOARD SUMMARY (FAAC stacked PR #602 vs Apple & fdk-aac 2.0.3)")
    print("====================================================================================================\n")

    summary_rows = []

    for ref_name in ['apple', 'fdk']:
        print(f"--- REFERENCE: {ref_name.upper()} ---")
        for rung, lo_str, hi_str in rungs:
            rf = f"probe/ladder/results/s7/{rung}_results.json"
            r = json.load(open(rf))
            lo, hi = 's' + lo_str, 's' + hi_str
            ctl = 'ctl'
            arm = ref_name

            d = {}
            for s in r[ctl]:
                # Slope calculation per clip
                sl = (r[hi][s]['mos'] - r[lo][s]['mos']) / math.log2(r[hi][s]['bytes'] / r[lo][s]['bytes'])
                d[s] = (r[arm][s]['mos'] - r[ctl][s]['mos']) - sl * math.log2(r[arm][s]['bytes'] / r[ctl][s]['bytes'])

            x = list(d.values())
            b = sum(r[arm][s]['bytes'] for s in d) / sum(r[ctl][s]['bytes'] for s in d) - 1.0
            raw = st.mean(r[arm][s]['mos'] for s in d) - st.mean(r[ctl][s]['mos'] for s in d)
            mean_adj = st.mean(x)
            med_adj = st.median(x)
            w_cnt = sum(y > 0.0005 for y in x)
            l_cnt = sum(y < -0.0005 for y in x)

            # Verdict calculation:
            # "FAAC beats" = mean adj <= -0.005 and W < L
            # "Par" = |mean adj| < 0.005
            # Otherwise reference leads
            if mean_adj <= -0.005 and w_cnt < l_cnt:
                verdict = "FAAC beats"
            elif abs(mean_adj) < 0.005:
                verdict = "Par"
            else:
                verdict = f"{ref_name.title()} leads"

            base_val = s5_s6_baseline[(ref_name, rung)]

            # Worst 5 clips for FAAC (where reference leads most, i.e. highest positive adj delta)
            # or worst clips for the rung.
            # Sorted by adj delta descending (highest positive delta = reference leads most / FAAC worst)
            o = sorted(d, key=d.get, reverse=True)
            worst_5 = [(s, d[s]) for s in o[:5]]

            summary_rows.append({
                'ref': ref_name,
                'rung': rung,
                'mean_adj': mean_adj,
                'med_adj': med_adj,
                'wl': f"{w_cnt}/{l_cnt}",
                'bytes_pct': b * 100.0,
                'verdict': verdict,
                'base_val': base_val,
                'worst_5': worst_5,
                'faac_mean_mos': st.mean(r[ctl][s]['mos'] for s in d),
                'ref_mean_mos': st.mean(r[arm][s]['mos'] for s in d)
            })

            print(f"Rung {rung.upper()} (vs {ref_name}):")
            print(f"  adj mean: {mean_adj:+.4f} | med: {med_adj:+.4f} | W/L: {w_cnt}/{l_cnt} | bytes: {100*b:+.1f}% | MOS: FAAC {st.mean(r[ctl][s]['mos'] for s in d):.3f} vs {ref_name} {st.mean(r[arm][s]['mos'] for s in d):.3f}")
            print(f"  Verdict: {verdict} (S5/S6 baseline: {base_val})")
            print(f"  5 Worst clips for FAAC (reference leads most):")
            for cs, cval in worst_5:
                print(f"    - {cs[:35]:<35}: adj {cval:+.4f}")
            print()

    print("====================================================================================================")
    print("10-CELL VERDICT TABLE")
    print("====================================================================================================")
    print(f"{'Rung':<8} | {'Ref':<8} | {'S5/S6 Base':<15} | {'S7-STACK Adj':<12} | {'Median':<10} | {'W/L':<8} | {'Bytes Diff':<10} | {'Verdict':<15}")
    print("-" * 100)
    for row in summary_rows:
        print(f"{row['rung'].upper():<8} | {row['ref'].title():<8} | {row['base_val']:<15} | {row['mean_adj']:+.4f}       | {row['med_adj']:+.4f}    | {row['wl']:<8} | {row['bytes_pct']:+.1f}%      | {row['verdict']:<15}")

if __name__ == '__main__':
    analyze()
