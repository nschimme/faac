import sys,glob,collections,os
cats='total hdr sect sf_reg sf_pns sf_is spec aux sbr fill nreg npns nis nzero'.split()
def clip(f):
    S=collections.defaultdict(list); fr={}
    for l in open(f):
        p=l.split()
        if p[0]=='S': S[int(p[1])].append(list(map(int,p[2:])))
        elif p[0]=='B': fr[int(p[1])]=list(map(int,p[2:]))
    acc={'long':[0]*len(cats),'short':[0]*len(cats)}; n={'long':0,'short':0}
    for k,b in fr.items():
        s=S.get(k)
        if not s: continue
        cl='short' if s[0][0] else 'long'
        # b: total hdr sect sf spec aux sbr ps fill
        v=[b[0],b[1],b[2],sum(x[1] for x in s),sum(x[2] for x in s),sum(x[3] for x in s),b[4],b[5],b[6]+b[7],b[8],
           sum(x[4] for x in s),sum(x[5] for x in s),sum(x[6] for x in s),sum(x[7] for x in s)]
        n[cl]+=1
        for i,x in enumerate(v): acc[cl][i]+=x
    return acc,n
for r in (16,24,32,48):
  print(f"== {r} kbps total, per-clip mean over 49 clips (long frames; short share)")
  for e in ('faac','fdk'):
    agg={'long':[0]*len(cats),'short':[0]*len(cats)}; nc={'long':0,'short':0}; shares=[]; N=0
    for f in sorted(glob.glob(f'p0/{r}/{e}/*.dump')):
        a,n=clip(f); tot=n['long']+n['short']
        if not tot: continue
        N+=1; shares.append(n['short']/tot)
        for cl in agg:
            if n[cl]:
                nc[cl]+=1
                for i in range(len(cats)): agg[cl][i]+=a[cl][i]/n[cl]
    for cl in ('long','short'):
        m=[x/max(nc[cl],1) for x in agg[cl]]
        side=m[2]+m[3]+m[4]+m[5]
        print(f"{e:4} {cl} clips={nc[cl]} short%={100*sum(shares)/N:.0f} "+' '.join(f"{c}={v:.0f}" for c,v in zip(cats,m))+f" | side(sect+sf)={side:.0f} ({100*side/max(m[0],1):.1f}%)")
