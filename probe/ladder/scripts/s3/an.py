import os,json,math,sys,statistics as stt
r=json.load(open(os.environ.get('SWEEP_W','/home/user/lw/sw')+'/results.json'));ctl=sys.argv[1] if len(sys.argv)>1 else 'ctl'
lo,hi=(sys.argv[2],sys.argv[3]) if len(sys.argv)>3 else ('s112','s144')
def sl(s):return (r[hi][s]['mos']-r[lo][s]['mos'])/math.log2(r[hi][s]['bytes']/r[lo][s]['bytes'])
key=['girl','Last_Of','liberate','take_your','velvet','bas','Changes','Shinsho','24-Green','Severance']
print(f"{'arm':10s} {'adj':>8s} {'med':>8s} W/L  {'bytes%':>6s} {'worst':>7s} "+' '.join(k[:7].rjust(7) for k in key))
for a in r:
    if a in (lo,hi) or len(r[a])<len(r[ctl]):continue
    d={s:(r[a][s]['mos']-r[ctl][s]['mos'])-sl(s)*math.log2(r[a][s]['bytes']/r[ctl][s]['bytes']) for s in r[ctl]}
    b=sum(r[a][s]['bytes'] for s in d)/sum(r[ctl][s]['bytes'] for s in d)-1
    v=list(d.values());ws=min(d,key=d.get)
    kv=[next((d[s] for s in d if s.startswith(k)),float('nan')) for k in key]
    print(f"{a:10s} {stt.mean(v):+.4f} {stt.median(v):+.4f} {sum(x>0.0005 for x in v):2d}/{sum(x<-0.0005 for x in v):2d} {100*b:+6.2f} {d[ws]:+.3f} {ws[:10]} "+' '.join(f'{x:+.3f}'.rjust(7) for x in kv))
