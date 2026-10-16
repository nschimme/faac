import json,sys,numpy as np
sys.path.insert(0,'../fb/scripts'); sys.path.insert(0,'../fb')
from bd_rate import bd_rate_curve
rng=np.random.default_rng(0)
def ci(x):
    x=np.array(x); b=[rng.choice(x,len(x)).mean() for _ in range(10000)]; return x.mean(),np.percentile(b,2.5),np.percentile(b,97.5)
fdk={(r['tag'],r['arm'],r['clip'],r['rate']):r['mos_fdk'] for r in json.load(open(sys.argv[1]))} if len(sys.argv)>1 else {}
for tag,f,R,arms in (('x244','rows_x244_x2c12_x2n12_x2n9_x2n13.json',(40,48,56,64),['x2n12','x2n9','x2n13']),('x232','rows_x232_x2c12_x2n12_x2n13_x2n9.json',(32,40,48,56),['x2n12','x2n13','x2n9'])):
    rows=json.load(open(f)); K={(r['arm'],r['clip'],r['rate']):r for r in rows}; clips=sorted({r['clip'] for r in rows})
    dec=[('faad2',lambda a,c,k:K[(a,c,k)]['mos']),('ffmpeg',lambda a,c,k:K[(a,c,k)]['mos_ff'])]
    if fdk: dec.append(('fdkdec',lambda a,c,k:fdk[(tag,a,c,k)]))
    for arm in arms:
        line=f'{tag} {arm[2:]:5s} vs c12'
        for dn,fn in dec:
            v=[]
            for c in clips:
                x=bd_rate_curve(sorted((K[('x2c12',c,k)]['kbps'],fn('x2c12',c,k)) for k in R),sorted((K[(arm,c,k)]['kbps'],fn(arm,c,k)) for k in R))
                if x is not None: v.append(x)
            m,lo,hi=ci(v); line+=f' | {dn} {m:+6.2f}% [{lo:+.2f},{hi:+.2f}] n={len(v)}'
        print(line)
    print(tag,'mean kbps/MOS(faad2) per rung:',{a[2:]:[(k,round(np.mean([K[(a,c,k)]['kbps'] for c in clips]),1),round(np.mean([K[(a,c,k)]['mos'] for c in clips]),3)) for k in R] for a in ['x2c12']+arms[:1]})
