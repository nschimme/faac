import json,sys,numpy as np
sys.path.insert(0,'../fb/scripts'); sys.path.insert(0,'../fb')
from bd_rate import bd_rate_curve
rng=np.random.default_rng(0)
def ci(x):
    x=np.array(x); b=[rng.choice(x,len(x)).mean() for _ in range(10000)]; return x.mean(),np.percentile(b,2.5),np.percentile(b,97.5)
def rep(tag,rows,R,fdk=None):
    K={(r['arm'],r['clip'],r['rate']):r for r in rows}; clips=sorted({r['clip'] for r in rows})
    fd={(r['arm'],r['clip'],r['rate']):r['mos_fdk'] for r in fdk if r['tag']==tag} if fdk else {}
    dec=[('faad2',lambda a,c,k:K[(a,c,k)]['mos']),('ffmpeg',lambda a,c,k:K[(a,c,k)].get('mos_ff',K[(a,c,k)]['mos']))]
    if fd: dec.append(('fdkdec',lambda a,c,k:fd[(a,c,k)]))
    for arm in ('fix11','fix9'):
        for dn,f in dec:
            if dn=='ffmpeg' and tag=='48k' and 'mos_ff' not in K[(arm,clips[0],R[0])]: continue
            v=[]
            for c in clips:
                try:
                    a=sorted((K[('fix15',c,k)]['kbps'],f('fix15',c,k)) for k in R); b=sorted((K[(arm,c,k)]['kbps'],f(arm,c,k)) for k in R)
                except TypeError: continue
                if any(p[1] is None for p in a+b): continue
                x=bd_rate_curve(a,b)
                if x is not None: v.append(x)
            m,lo,hi=ci(v); line=f'{tag:5s} {arm:5s} {dn:6s} BD n={len(v)} {m:+6.2f}% [{lo:+.2f},{hi:+.2f}] |'
            for k in R:
                d=[f(arm,c,k)-f('fix15',c,k) for c in clips if f(arm,c,k) is not None and f('fix15',c,k) is not None]; m2,lo2,hi2=ci(d)
                line+=f' {k}:{m2:+.3f}[{lo2:+.3f},{hi2:+.3f}] {sum(x>0 for x in d)}/{sum(x<=0 for x in d)}'
            print(line)
if __name__=='__main__':
    fdk=json.load(open(sys.argv[1])) if len(sys.argv)>1 else None
    rep('44k1',json.load(open('rows_44k1_fix15_fix9_fix11.json')),(40,48,56,64),fdk)
    rep('32k',json.load(open('rows_32k_fix15_fix9_fix11.json')),(32,40,48,56),fdk)
