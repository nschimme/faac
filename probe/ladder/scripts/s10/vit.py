import re,glob,collections,sys,statistics as st
src=open('/Users/nschimme/gitprojects/faac/libfaac/huffdata.c').read()
body=src[src.index('book12[121]'):]; body=body[:body.index('};')]
L=[int(a) for a,b in re.findall(r'\{\s*(\d+)\s*,\s*(0x[0-9a-fA-F]+|\d+)\s*\}',body)]
dl=lambda d:L[60+max(-60,min(60,d))]
def price(seq):
    return 0 if not seq else 9+sum(dl(b-a) for a,b in zip(seq,seq[1:]))
def vit(seq,tol):  # tol[i] = max step move for band i
    if len(seq)<2: return seq
    prev={0:0}; back=[]
    # state = offset of band i; band 0 fixed (absolute, constant cost)
    for i in range(1,len(seq)):
        t=tol[i]; cur={}; bp={}
        for o in range(-t,t+1):
            best=None
            for po,c in prev.items():
                v=c+dl((seq[i]+o)-(seq[i-1]+po))
                if best is None or v<best[0]: best=(v,po)
            cur[o]=best[0]; bp[o]=best[1]
        back.append(bp); prev=cur
    o=min(prev,key=prev.get); offs=[o]
    for bp in reversed(back): o=bp[o]; offs.append(o)
    offs=offs[::-1]
    return [seq[0]]+[seq[i]+offs[i-1] for i in range(1,len(seq))] if False else [seq[0]]+[seq[i]+offs[i] for i in range(1,len(seq))]
sl=lambda n:[int(x) for x in re.search(rf'sfb_{n}\[\] = \{{([^}}]*)\}}',open('/Users/nschimme/gitprojects/faac/common/sfb_tables.c').read()).group(1).replace('\n',' ').split(',') if x.strip()]
LONG=sl('1024_24000'); SHORT=sl('128_24000')
def run(d,r,label):
    acc={k:0 for k in ('t1','t2','fd')}; ftot=0; nf=0; bad=0; nb=0; pns=0; nfr=0
    for f in glob.glob(f'{d}/{r}/faac/*.dump'):
        S=collections.defaultdict(list); C=collections.defaultdict(list); B={}
        for l in open(f):
            p=l.split()
            if p[0]=='S': S[int(p[1])].append(list(map(int,p[2:])))
            elif p[0]=='B': B[int(p[1])]=int(p[2])
            elif p[0]=='C':
                groups=l.split('|')[1].split('/')
                C[int(p[1])].append((p[4]=='2',groups))
        for k,ics in C.items():
            if k not in S or len({w for w,_ in ics})!=1 or ics[0][0]!=(label=='short'): continue
            fr_saved={x:0 for x in acc}
            for (w,groups),s in zip(ics,S[k]):
                seq=[];fq=[]
                for g,gr in enumerate(groups):
                    for bi,b in enumerate(gr.split()):
                        c,e=b.split(':')[:2]
                        if c=='13':
                            seq.append(int(e)); 
                            fq.append((SHORT[bi]*12000/128) if w else (LONG[bi]*12000/1024))
                p0=price(seq)
                if p0!=s[2]: bad+=1
                nb+=1; pns+=p0
                fr_saved['t1']+=p0-price(vit(seq,[1]*len(seq)))
                fr_saved['t2']+=p0-price(vit(seq,[2]*len(seq)))
                fr_saved['fd']+=p0-price(vit(seq,[1 if q<6000 else 2 for q in fq]))
            for x in acc: acc[x]+=fr_saved[x]
            ftot+=B[k]; nfr+=1
    if nfr: print(f"{label:5} {r:2}k [{d}] frames={nfr} ctl_bad={bad}/{nb} pnsbits/frame={pns/nfr:.0f} frame={ftot/nfr:.0f} | "+' '.join(f"{x}: {acc[x]/nfr:.0f}b={100*acc[x]/ftot:.1f}%" for x in acc))
for r in (16,24,32,48):
    run('p0',r,'short'); run('p0',r,'long')
for r in (16,24): run('p0n',r,'long')
