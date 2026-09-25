#!/usr/bin/env python3
"""Report measured combinations without mixing baselines or metric engines."""
import argparse
import json
import math
from pathlib import Path
import statistics as st

def read(root):
    return {(r['profile'],r['rate'],r['arm'],r['clip']):r for p in Path(root).glob('*/*/*.json') if (r:=json.loads(p.read_text())).get('arm')}

def compare(rows, profile, rate, before, after, slope_arm):
    clips = sorted(k[3] for k in rows if k[:3] == (profile,rate,before))
    assert clips and all((profile,rate,after,c) in rows for c in clips)
    result = dict(profile=profile, rate=rate, before=before, after=after, n=len(clips), slope_arm=slope_arm)
    rs = sorted({k[1] for k in rows if k[0] == profile and k[2] == slope_arm})
    i = rs.index(rate)
    lo, hi = rs[max(0,i-1)], rs[min(len(rs)-1,i+1)]
    assert lo != hi, 'Need a same-profile calibration ladder'
    for decoder in ['ffmpeg','fdk']:
        result[decoder] = {}
        for metric in ['visqol','zimtohrli']:
            values = []
            for clip in clips:
                b = rows[profile,rate,before,clip]; a = rows[profile,rate,after,clip]
                low = rows[profile,lo,slope_arm,clip]; high = rows[profile,hi,slope_arm,clip]
                denominator = math.log2(high['payload_bytes']/low['payload_bytes'])
                slope = (high[decoder][metric]-low[decoder][metric])/denominator if denominator else 0
                raw = a[decoder][metric]-b[decoder][metric]
                adjusted = raw-slope*math.log2(a['payload_bytes']/b['payload_bytes'])
                values.append(dict(clip=clip,raw=raw,adjusted=adjusted,slope=slope))
            result[decoder][metric] = dict(
                before=st.mean(rows[profile,rate,before,c][decoder][metric] for c in clips),
                after=st.mean(rows[profile,rate,after,c][decoder][metric] for c in clips),
                raw=st.mean(v['raw'] for v in values), adjusted=st.mean(v['adjusted'] for v in values),
                wins=sum(v['raw'] > .02 for v in values), losses=sum(v['raw'] < -.02 for v in values),
                adjusted_wins=sum(v['adjusted'] > .02 for v in values), adjusted_losses=sum(v['adjusted'] < -.02 for v in values),
                worst=sorted(values,key=lambda v:v['adjusted'])[:5], clips=values)
        for metric in ['ic','ichi']:
            valid = [c for c in clips if rows[profile,rate,before,c][decoder][metric] is not None and rows[profile,rate,after,c][decoder][metric] is not None]
            b = st.mean(rows[profile,rate,before,c][decoder][metric] for c in valid)
            a = st.mean(rows[profile,rate,after,c][decoder][metric] for c in valid)
            result[decoder][metric] = dict(before=b,after=a,relative=a/b-1 if b else None,n=len(valid))
    for metric in ['bytes','payload_bytes']:
        b = sum(rows[profile,rate,before,c][metric] for c in clips)
        a = sum(rows[profile,rate,after,c][metric] for c in clips)
        result[metric] = dict(before=b,after=a,relative=a/b-1)
    return result

def main():
    ap=argparse.ArgumentParser(description=__doc__); ap.add_argument('directory'); ap.add_argument('output')
    ap.add_argument('--baseline',default='base'); ap.add_argument('--candidates',default='sbr,stereo,stack')
    a=ap.parse_args(); rows=read(a.directory); reports=[]
    scenarios=sorted({k[:2] for k in rows})
    for profile,rate in scenarios:
        previous=a.baseline
        for arm in a.candidates.split(','):
            reports.append(compare(rows,profile,rate,a.baseline,arm,a.baseline))
            if previous != a.baseline: reports.append(compare(rows,profile,rate,previous,arm,a.baseline))
            previous=arm
        if (profile,rate,'apple',next(k[3] for k in rows if k[:3] == (profile,rate,a.baseline))) in rows:
            for other in ['apple','fdk']: reports.append(compare(rows,profile,rate,other,'stack','stack'))
    Path(a.output).write_text(json.dumps(reports,indent=2)+'\n')
    for r in reports:
        print(f"{r['profile']}{r['rate']} {r['before']} -> {r['after']} n={r['n']} bytes {r['bytes']['relative']:+.2%}",end='')
        for d in ['ffmpeg','fdk']:
            for m in ['visqol','zimtohrli']:
                x=r[d][m]; print(f" | {d}/{m}: {x['after']:.4f} d{x['raw']:+.4f} adj{x['adjusted']:+.4f} W/L {x['wins']}/{x['losses']}",end='')
            print(f" IC {r[d]['ic']['relative']:+.1%} HI {r[d]['ichi']['relative']:+.1%}",end='')
        print()

if __name__ == '__main__': main()
