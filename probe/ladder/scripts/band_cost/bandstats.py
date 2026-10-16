import sys,collections
def load(p):
    ics=[];cur=None
    for l in open(p):
        t=l.split()
        if t[0]=='S': cur={'win':int(t[1]),'ng':int(t[2]),'ms':int(t[3]),'gg':int(t[4]),'b':[]}; ics.append(cur)
        else:
            g,b,s,e,cb,sf,sfb,spb,nz,sa,mq=map(int,t[1:]); cur['b'].append((g,b,s,e,cb,sf,sfb,spb,nz,sa,mq))
    return ics
REG=[(0,6000),(6000,12000),(12000,16000),(16000,24000)]
def stats(p,short=False):
    ics=[i for i in load(p) if (i['win']==2)==short]
    R=[collections.defaultdict(float) for _ in REG]
    n=len(ics)
    for i in ics:
        for (g,b,s,e,cb,sf,sfb,spb,nz,sa,mq) in i['b']:
            f=(s if not short else s*8)*24000/1024.0
            for ri,(lo,hi) in enumerate(REG):
                if lo<=f<hi:
                    r=R[ri]; w=(e-s)
                    r['bands']+=1; r['sfbits']+=sfb; r['specbits']+=spb
                    if cb==0: r['zero']+=1
                    elif cb==13: r['pns']+=1
                    elif cb>=14: r['is']+=1
                    else:
                        r['coded']+=1; r['lines']+=w*(1 if short else 1); r['nz']+=nz; r['sumabs']+=sa
                        r['cbsum']+=cb; r['maxq']+=mq
                        r['cb_%d'%cb]+=1
    return n,R
if __name__=='__main__':
    for p in sys.argv[1:]:
        for sh in (False,True):
            n,R=stats(p,sh)
            print('==',p,'short' if sh else 'long','ics',n)
            for (lo,hi),r in zip(REG,R):
                if not r['bands']: continue
                nb=r['bands'];lines=max(r['lines'],1)
                print(f"{lo/1000:>4.0f}-{hi/1000:<3.0f}k bands/ics {nb/n:5.1f} zero {r['zero']/nb:5.1%} pns {r['pns']/nb:5.1%} is {r['is']/nb:5.1%} coded {r['coded']/nb:5.1%} | specbits/ics {r['specbits']/n:7.1f} sfbits/ics {r['sfbits']/n:6.1f} | bits/line {r['specbits']/lines:5.2f} nz {r['nz']/lines:5.1%} mean|q|nz {r['sumabs']/max(r['nz'],1):4.2f} avg maxq {r['maxq']/max(r['coded'],1):5.2f} sfbits/band {r['sfbits']/max(r['coded']+r['pns']+r['is'],1):4.2f}")
