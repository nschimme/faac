import re,glob,collections,sys,statistics as st
src=open('/Users/nschimme/gitprojects/faac/libfaac/huffdata.c').read()
body=src[src.index('book12[121]'):]; body=body[:body.index('};')]
ent=re.findall(r'\{\s*(\d+)\s*,\s*(0x[0-9a-fA-F]+|\d+)\s*\}',body)
print('book12 entries',len(ent),'sample',ent[:3],file=sys.stderr)
L=[int(a) for a,b in ent]  # len first?
def dl(d): return L[60+max(-60,min(60,d))]
def price(seq):  # seq = PNS energies in band order for one ICS
    if not seq: return 0
    b=9; prev=seq[0]
    for e in seq[1:]: b+=dl(e-prev); prev=e
    return b
def clampK(seq,K):
    out=[seq[0]]
    for e in seq[1:]: out.append(out[-1]+max(-K,min(K,e-out[-1])))   # follows the original, limited slew
    return out
def snap(seq,g): return [round(e/g)*g for e in seq]
rules={'clamp1':lambda s:clampK(s,1),'clamp2':lambda s:clampK(s,2),'clamp4':lambda s:clampK(s,4),'grid2':lambda s:snap(s,2),'grid3':lambda s:snap(s,3)}
for r in (32,48):
    tot=0; ctl_bad=0; n=0; ctl=0; frames=set(); acc={k:[0,[],0] for k in rules}; ftot=0
    for f in glob.glob(f'p0/{r}/faac/*.dump'):
        S=collections.defaultdict(list); C=collections.defaultdict(list); B={}
        for l in open(f):
            p=l.split()
            if p[0]=='S': S[int(p[1])].append(list(map(int,p[2:])))
            elif p[0]=='B': B[int(p[1])]=int(p[2])
            elif p[0]=='C':
                bands=[b.split(':') for b in l.split('|')[1].split('/')[0].split()]
                C[int(p[1])].append((p[4],[(int(b[0]),int(b[1])) for b in bands]))
        for k,ics in C.items():
            if k not in S or any(w=='2' for w,_ in ics): continue
            nl=0
            for (w,bands),s in zip(ics,S[k]):
                seq=[e for c,e in bands if c==13]
                p0=price(seq); n+=1
                if p0!=s[2]: ctl_bad+=1
                ctl+=p0
                for name,fn in rules.items():
                    if seq:
                        new=fn(seq); acc[name][0]+=p0-price(new)
                        acc[name][1]+= [abs(a-b)*1.5 for a,b in zip(seq,new)]
            ftot+=B[k]; frames.add((f,k))
    nf=len(frames)
    print(f"== {r}k FAAC long frames={nf} ICS={n}; control price!=S: {ctl_bad}; mean pns bits/frame={ctl/nf:.0f}; mean frame bits={ftot/nf:.0f}")
    for name,(sv,err,_) in acc.items():
        print(f"  {name:7} saves {sv/nf:5.1f} bits/frame ({100*sv/ftot:.1f}% of frame)  mean|err|={st.mean(err):.2f} dB  max={max(err):.1f} dB")
