import re,glob,collections,statistics as st,sys,json
src=open('/Users/nschimme/gitprojects/faac/common/sfb_tables.c').read()
LONG=[int(x) for x in re.search(r'sfb_1024_24000\[\] = \{([^}]*)\}',src).group(1).replace('\n',' ').split(',') if x.strip()]
HZ=12000/1024; EDG=[0,2000,4000,6000,8250]; RN=['0-2k','2-4k','4-6k','6-8.25k']
reg=lambda i:next((k for k in range(4) if EDG[k]<=LONG[i]*HZ<EDG[k+1]),None)
def clip(f):
    C={}; B={}
    for l in open(f):
        p=l.split()
        if p[0]=='C': C.setdefault(int(p[1]),[]).append((p[4],[b.split(':') for b in l.split('|')[1].split('/')[0].split()]))
        elif p[0]=='B': B[int(p[1])]=list(map(int,p[2:]))
    cnt=[collections.Counter() for _ in range(4)]; nzs=[0]*4; cls=[0]*4; cbsum=[0]*4; cbn=[0]*4
    nl=ns=0; tns=0; ms_c=0; ms_t=0; fl=0; fl_n=0; prev=None
    for k in sorted(C):
        if k not in B: continue
        ics=C[k]
        if any(w=='2' for w,_ in ics): ns+=1; prev=None; continue
        nl+=1
        if B[k][5]>6*len(ics)//2*1: pass
        if B[k][5]>3*len(ics): tns+=1
        cur=[]
        for w,bs in ics:
            codedflags=[]
            for i,b in enumerate(bs):
                c=int(b[0]); r=reg(i); wd=LONG[i+1]-LONG[i]
                is_c=1<=c<=11; codedflags.append(is_c)
                if is_c: ms_t+=1; ms_c+=int(b[3])>0
                if r is None: continue
                cnt[r]['pns' if c==13 else 'is' if c in (14,15) else 'zero' if c==0 else 'coded']+=wd
                if is_c: nzs[r]+=int(b[2]); cls[r]+=wd; cbsum[r]+=c; cbn[r]+=1
            cur.append(codedflags)
        if prev is not None and len(prev)==len(cur):
            for pa,ca in zip(prev,cur):
                n=min(len(pa),len(ca))
                for i in range(n):
                    if reg(i) is None: continue
                    fl_n+=1; fl+= pa[i]!=ca[i]
        prev=cur
    return dict(nl=nl,ns=ns,cnt=cnt,nz=nzs,cl=cls,cbs=cbsum,cbn=cbn,tns=tns,ms=(ms_c,ms_t),fl=(fl,fl_n))
def agg(pat):
    per={}
    for f in sorted(glob.glob(pat)):
        d=clip(f)
        if d['nl']: per[f.split('/')[-1][:-5]]=d
    return per
def summ(per):
    R=[]
    for r in range(4):
        tot=sum(sum(d['cnt'][r].values()) for d in per.values()) or 1
        c=lambda k:100*sum(d['cnt'][r][k] for d in per.values())/tot
        cl=sum(d['cl'][r] for d in per.values()) or 1
        R.append(f"{RN[r]}: c{c('coded'):.0f} p{c('pns'):.0f} i{c('is'):.0f} z{c('zero'):.0f} d{sum(d['nz'][r] for d in per.values())/cl:.2f} cb{sum(d['cbs'][r] for d in per.values())/max(sum(d['cbn'][r] for d in per.values()),1):.1f}")
    nl=sum(d['nl'] for d in per.values()); ns=sum(d['ns'] for d in per.values())
    fl=sum(d['fl'][0] for d in per.values())/max(sum(d['fl'][1] for d in per.values()),1)
    ms=sum(d['ms'][0] for d in per.values())/max(sum(d['ms'][1] for d in per.values()),1)
    tn=sum(d['tns'] for d in per.values())/nl
    return ' | '.join(R)+f" || short {100*ns/(ns+nl):.0f}% TNS-on-long {100*tn:.0f}% M/S(of coded) {100*ms:.0f}% flicker {100*fl:.1f}%"
if __name__=='__main__':
    for tag,pat in [('fdk','p0/48/fdk/*.dump'),('apple','p0/48/apple/*.dump'),('b12p4','arm/b12p4/48/*.dump'),('b12p2','arm/b12p2/48/*.dump'),('b12p1','arm/b12p1/48/*.dump')]:
        print(f"{tag:6}",summ(agg(pat)))
