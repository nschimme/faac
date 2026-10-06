import re,glob,collections,statistics as st,sys
src=open('/Users/nschimme/gitprojects/faac/common/sfb_tables.c').read()
LONG=[int(x) for x in re.search(r'sfb_1024_24000\[\] = \{([^}]*)\}',src).group(1).replace('\n',' ').split(',') if x.strip()]
def stats(pat):
    rows=[]
    for f in sorted(glob.glob(pat)):
        C=collections.defaultdict(list); B={}; S=collections.defaultdict(list)
        for l in open(f):
            p=l.split()
            if p[0]=='C': C[int(p[1])].append((p[4],l.split('|')[1].split('/')[0].split()))
            elif p[0]=='B': B[int(p[1])]=list(map(int,p[2:]))
            elif p[0]=='S': S[int(p[1])].append(list(map(int,p[2:])))
        a=collections.Counter(); nl=ns=0; cl=nz=0; side=spec=tot=0
        for k,ics in C.items():
            if k not in B: continue
            if any(w=='2' for w,_ in ics): ns+=1; continue
            nl+=1; b=B[k]; side+=b[2]+b[3]; spec+=b[4]; tot+=b[0]
            for w,bs in ics:
                for i,bb in enumerate(bs):
                    c,s,n,_=bb.split(':'); c=int(c); wd=LONG[i+1]-LONG[i]
                    a['pns' if c==13 else 'is' if c in(14,15) else 'zero' if c==0 else 'coded']+=wd
                    if 1<=c<=11: cl+=wd; nz+=int(n)
        used=a['coded']+a['pns']+a['is']+a['zero']
        if nl and cl: rows.append(dict(short=ns/(ns+nl),coded=a['coded']/used,pns=a['pns']/used,zero=a['zero']/used,isb=a['is']/used,nnz=nz/cl,side=side/tot,spec=spec/nl,tot=tot/nl,above=1-used/(2048*nl)))
    return rows
if __name__=='__main__':
    for r in (32,48):
        print(f'== {r}k  (coded/PNS/IS/zero = % of lines below max_sfb)')
        for tag in ['b15p4','b15p3','b15p2','b15p1','b12p4','b12p3','b12p2','b12p1']:
            rows=stats(f'arm/{tag}/{r}/*.dump')
            if not rows: continue
            m={k:st.mean(x[k] for x in rows) for k in rows[0]}
            mb=sum(len(open(f,'rb').read()) for f in glob.glob(f'arm/{tag}/{r}/*.aac'))/1e6
            print(f"{tag}: short {100*m['short']:.0f}% | coded {100*m['coded']:.0f} PNS {100*m['pns']:.0f} IS {100*m['isb']:.0f} zero {100*m['zero']:.0f} | nnz/line {m['nnz']:.2f} | side(sect+sf) {100*m['side']:.1f}% of frame | spec {m['spec']:.0f} tot {m['tot']:.0f} | {mb:.3f}MB")
        rows=stats(f'p0/{r}/fdk/*.dump'); m={k:st.mean(x[k] for x in rows) for k in rows[0]}
        print(f"fdk  : short {100*m['short']:.0f}% | coded {100*m['coded']:.0f} PNS {100*m['pns']:.0f} IS {100*m['isb']:.0f} zero {100*m['zero']:.0f} | nnz/line {m['nnz']:.2f} | side {100*m['side']:.1f}% | spec {m['spec']:.0f} tot {m['tot']:.0f}")
