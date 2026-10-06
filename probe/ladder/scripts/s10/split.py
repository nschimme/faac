import re,glob,collections,statistics as st,sys
src=open('/Users/nschimme/gitprojects/faac/common/sfb_tables.c').read()
LONG=[int(x) for x in re.search(r'sfb_1024_24000\[\] = \{([^}]*)\}',src).group(1).replace('\n',' ').split(',') if x.strip()]
def split(pat):
    rows=[]
    for f in sorted(glob.glob(pat)):
        a=collections.Counter(); nl=ns=0; cl=nz=sp=nf=0; pb=0; fb=0; frames=0
        C=collections.defaultdict(list); B={}; S={}
        for l in open(f):
            p=l.split()
            if p[0]=='C': C[int(p[1])].append((p[4],l.split('|')[1].split('/')[0].split()))
            elif p[0]=='B': B[int(p[1])]=list(map(int,p[2:]))
        tot=len(B)
        for k,ics in C.items():
            if k not in B: continue
            if any(w=='2' for w,_ in ics): ns+=1; continue
            nl+=1; sp+=B[k][4]
            for w,bs in ics:
                cov=0
                for i,b in enumerate(bs):
                    c,s,n,_=b.split(':'); c=int(c); wd=LONG[i+1]-LONG[i]; cov+=wd
                    a['pns' if c==13 else 'is' if c in(14,15) else 'zero' if c==0 else 'coded']+=wd
                    if 1<=c<=11: cl+=wd; nz+=int(n)
                a['beyond']+=1024-cov
        if nl and cl: rows.append(dict(short=ns/(ns+nl),**{k:a[k]/nl/1024/ (len(ics)) if False else a[k]/nl/2048 for k in ('coded','pns','is','zero','beyond')},bpl=sp/cl*1.0*0+sp/ (cl),nnz=nz/cl))
    return rows
for r in (32,48):
    for tag,pat in [(f'faac bsf{v}',f'bsf/{v}/{r}/faac/*.dump') for v in (15,12,10,8)]+[('fdk',f'p0/{r}/fdk/*.dump')]:
        rows=split(pat)
        if not rows: continue
        m={k:st.mean(x[k] for x in rows) for k in rows[0]}
        print(f"{r}k {tag:10} short {100*m['short']:3.0f}% | long ICS core %: coded {100*m['coded']:3.0f} PNS {100*m['pns']:3.0f} IS {100*m['is']:3.0f} zero {100*m['zero']:3.0f} above-max_sfb {100*m['beyond']:3.0f} | nnz/coded line {m['nnz']:.2f}")
