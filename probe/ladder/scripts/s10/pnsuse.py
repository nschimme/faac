import re,glob,collections,statistics as st
src=open('/Users/nschimme/gitprojects/faac/common/sfb_tables.c').read()
sl=lambda n:[int(x) for x in re.search(rf'sfb_{n}\[\] = \{{([^}}]*)\}}',src).group(1).replace('\n',' ').split(',') if x.strip()]
LONG=sl('1024_24000'); SHORT=sl('128_24000')
edges=[0,2000,4000,6000,8000,12001]
def run(r,e):
    per=[]; reg=collections.Counter(); ics_n=0; tot=0; bits=0; fr=0; maxsfb=[]
    for f in sorted(glob.glob(f'p0/{r}/{e}/*.dump')):
        n=pl=cl=0; rg=collections.Counter(); sb=0; fb=0; frames=0; mx=[]
        for l in open(f):
            p=l.split()
            if p[0]=='C':
                short=p[4]=='2'; off=SHORT if short else LONG; W=128 if short else 1024
                groups=l.split('|')[1].split('/')
                for gr in groups:
                    bs=gr.split()
                    for i,b in enumerate(bs):
                        c=int(b.split(':')[0]); lo,hi=off[i],off[i+1]
                        # lines in this group band; short windows: group repeats per window, weight by lines/W of one window
                        w=(hi-lo)/W
                        n+=w if False else 0
                        fq=lo*12000/W
                        if c==13:
                            pl+=w; rg[next(k for k in range(5) if edges[k]<=fq<edges[k+1])]+=w
                        if c!=0: cl+=w
                mx.append(p[5])
                n+=len(groups)*0+1   # one ICS (all groups) counted once
            elif p[0]=='S': sb+=int(p[4])
            elif p[0]=='B': fb+=int(p[2]); frames+=1
        if n: per.append((pl/n, cl/n, sb/frames, fb/frames, rg, n))
    return per
for r in (32,48):
  for e in ('faac','fdk','apple'):
    per=run(r,e)
    if not per: continue
    pl=st.mean(x[0] for x in per); cl=st.mean(x[1] for x in per)
    tot=sum(sum(x[4].values()) for x in per)
    split=[sum(x[4][k] for x in per)/tot*100 if tot else 0 for k in range(5)]
    print(f"{r}k {e:5} PNS coverage per ICS: {pl:.2f}/{cl:.2f} of unit(=window-equivalents of 1 per-band-width weight)  pns bits/frame {st.mean(x[2] for x in per):.0f} of {st.mean(x[3] for x in per):.0f} ({100*st.mean(x[2]/x[3] for x in per):.1f}%)  PNS by freq 0-2/2-4/4-6/6-8/8-12k: "+'/'.join(f'{s:.0f}' for s in split)+'%')
