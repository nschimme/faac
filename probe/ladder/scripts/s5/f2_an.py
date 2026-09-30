"""F2 table: fdk vs FAAC ctl, bits-adjusted with FAAC's slope s<lo>/s<hi>.  usage: f2_an.py <W> <lo> <hi> [arm=fdk] [ctl=ctl]"""
import sys,json,math,statistics as st
W,lo,hi=sys.argv[1:4];arm=sys.argv[4] if len(sys.argv)>4 else 'fdk';ctl=sys.argv[5] if len(sys.argv)>5 else 'ctl'
r=json.load(open(W+'/results.json'));lo,hi='s'+lo,'s'+hi
d={}
for s in r[ctl]:
    sl=(r[hi][s]['mos']-r[lo][s]['mos'])/math.log2(r[hi][s]['bytes']/r[lo][s]['bytes'])
    d[s]=(r[arm][s]['mos']-r[ctl][s]['mos'])-sl*math.log2(r[arm][s]['bytes']/r[ctl][s]['bytes'])
x=list(d.values());b=sum(r[arm][s]['bytes'] for s in d)/sum(r[ctl][s]['bytes'] for s in d)-1
raw=st.mean(r[arm][s]['mos'] for s in d)-st.mean(r[ctl][s]['mos'] for s in d)
o=sorted(d,key=d.get)
print(f"{W.split('_')[-1]} n={len(x)} adj {st.mean(x):+.4f} med {st.median(x):+.4f} W/L {sum(y>0.0005 for y in x)}/{sum(y<-0.0005 for y in x)} "
      f"bytes {100*b:+.1f}% raw {raw:+.4f} ({st.mean(r[ctl][s]['mos'] for s in d):.3f} vs {st.mean(r[arm][s]['mos'] for s in d):.3f})")
print('  FAAC best:',', '.join(f'{s[:18]} {d[s]:+.3f}' for s in o[:4]));print('  fdk best: ',', '.join(f'{s[:18]} {d[s]:+.3f}' for s in o[-4:][::-1]))
