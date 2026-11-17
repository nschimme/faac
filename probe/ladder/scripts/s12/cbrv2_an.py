import json,sys,collections,random
R=json.load(open(sys.argv[1]))
d=collections.defaultdict(dict)
for r in R:
    if not r.get('decode_valid') or r.get('mos') is None: continue
    arm='v2' if r['tool'].endswith('-dirty)') or 'dirty' in r['tool'] else 'm'
    d[(r['scenario'],r['profile'])].setdefault(r['filename'],{})[arm]=(r['mos'],r['actual_bitrate'] or 0)
print('| scenario | profile | n | mean dMOS [95% CI] | worst clip dMOS | <-0.10 | <-0.05 | dkbps |')
print('|---|---|---|---|---|---|---|---|')
tot10=0;worst=[]
for k in sorted(d):
    pairs=[(f,v['v2'][0]-v['m'][0],v['v2'][1]-v['m'][1]) for f,v in d[k].items() if 'v2' in v and 'm' in v]
    if not pairs: continue
    ds=[p[1] for p in pairs];n=len(ds);m=sum(ds)/n
    rnd=random.Random(0);bs=sorted(sum(rnd.choice(ds) for _ in ds)/n for _ in range(10000))
    w=min(pairs,key=lambda p:p[1]);c10=sum(x<-0.10 for x in ds);c05=sum(x<-0.05 for x in ds);tot10+=c10
    worst+= [(p[1],k,p[0]) for p in pairs if p[1]<-0.05]
    print(f'| {k[0]} | {k[1]} | {n} | {m:+.4f} [{bs[250]:+.4f},{bs[9750]:+.4f}] | {w[1]:+.3f} ({w[0][:28]}) | {c10} | {c05} | {sum(p[2] for p in pairs)/n:+.2f} |')
print(f'\ncells < -0.10: {tot10}')
for x in sorted(worst)[:15]: print(f'{x[0]:+.3f} {x[1]} {x[2]}')
