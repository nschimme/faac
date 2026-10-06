import glob,collections,statistics as st
off=[0,4,8,12,16,20,24,28,32,36,40,44,48,52,56,60,64,68,72,76,80,88,96,108,120,132,144,160,176,196,216,240,264,292,320,352,384,416,448,480,512,544,576,608,640,672,704,736,768,800,832,864,896,928,1024]
import re
src=open('/Users/nschimme/gitprojects/faac/common/sfb_tables.c').read()
m=re.search(r'sfb_1024_24000\[\] = \{([^}]*)\}',src); off=[int(x) for x in m.group(1).replace('\n',' ').split(',') if x.strip()]
hz=lambda b: off[b]*12000/1024   # core 24 kHz: 1024 lines span 12 kHz
for r in (16,24,32,48):
  for e in ('faac','fdk'):
    kxs=[];tops=[];ab=[];n=0;ncl=0
    for f in glob.glob(f'p0/{r}/{e}/*.dump'):
        kx=None
        for l in open(f):
            p=l.split()
            if p[0]=='H': kx=int(p[15])
            elif p[0]=='C' and p[4]!='2' and kx is not None:
                cbs=[int(b.split(':')[0]) for b in l.split('|')[1].split('/')[0].split()]
                top=max([i for i,c in enumerate(cbs) if c!=0],default=-1)
                kxs.append(kx*375); tops.append(hz(top+1) if top>=0 else 0); n+=1
                ab.append(sum(1 for i,c in enumerate(cbs) if off[i]*12000/1024>=kx*375 and c!=0))
    if n: print(r,e,f"long ICS={n} kx_Hz mean={st.mean(kxs):.0f} topcoded_Hz mean={st.mean(tops):.0f} median={st.median(tops):.0f}; coded bands at/above kx: {st.mean(ab):.2f}/ICS, share ICS with any={100*sum(1 for a in ab if a)/n:.0f}%")
