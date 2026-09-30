"""B3 fit: y = Apple sf - FAAC sf on the 0-6 kHz set vs within-frame FAAC features, linear, 2-fold by clip parity.
Writes <G>/b3_pred.json {clip: {"f c fb": pred_int}} for every row (set and non-set), cross-fitted.
usage: b3_fit.py <G> [feature,feature,...]"""
import sys,json,pathlib,collections,numpy as np
G=pathlib.Path(sys.argv[1]);rows=json.load(open(G/'b3_rows.json'))
F=(sys.argv[2] if len(sys.argv)>2 else 'relE,mE,tonal,lw,smr,nres,relsf,khz,short').split(',')
def x(r):
    d=dict(r,khz=r['hz']/1000.0,lkhz=np.log2(max(r['hz'],50)/1000.0))
    return [1.0]+[d[f] for f in F]
fold=lambda r:int(r['clip'].split('_')[-1])%2
S=[r for r in rows if r['in_set']]
print(f'rows {len(rows)} set {len(S)} (short {sum(r["short"] for r in S)}); y mean {np.mean([r["y"] for r in S]):+.2f} sd {np.std([r["y"] for r in S]):.2f}')
models={}
for k in (0,1):
    tr=[r for r in S if fold(r)!=k];te=[r for r in S if fold(r)==k]
    X=np.array([x(r) for r in tr]);y=np.array([r['y'] for r in tr]);b,*_=np.linalg.lstsq(X,y,rcond=None);models[k]=b
    Xt=np.array([x(r) for r in te]);yt=np.array([r['y'] for r in te]);p=Xt@b
    r2=1-np.sum((yt-p)**2)/np.sum((yt-yt.mean())**2);r2d=1-np.sum((yt-p)**2)/np.sum(yt**2)
    # "demeaned within ICS" R2: how well the shape is captured
    grp=collections.defaultdict(list)
    for r,pp in zip(te,p):grp[(r['clip'],r['f'],r['c'])].append((r['y'],pp))
    num=sum(sum(((a-np.mean([q[0] for q in v]))-(b_-np.mean([q[1] for q in v])))**2 for a,b_ in v) for v in grp.values())
    den=sum(sum((a-np.mean([q[0] for q in v]))**2 for a,_ in v) for v in grp.values())
    print(f'test fold {k}: n {len(te)} R2 {r2:.3f} (vs 0: {r2d:.3f}) within-ICS R2 {1-num/den:.3f} | '+' '.join(f'{n}={c:+.3f}' for n,c in zip(['c']+F,b)))
pred=collections.defaultdict(dict)
for r in rows:pred[r['clip']][f"{r['f']} {r['c']} {r['fb']}"]=int(np.rint(np.dot(x(r),models[fold(r)])))
json.dump(pred,open(G/'b3_pred.json','w'))
P=[pred[r['clip']][f"{r['f']} {r['c']} {r['fb']}"] for r in S];Y=[r['y'] for r in S]
print('set: mean pred %+.2f, exact %.1f%%, |err|<=1 %.1f%%'%(np.mean(P),100*np.mean([p==y for p,y in zip(P,Y)]),100*np.mean([abs(p-y)<=1 for p,y in zip(P,Y)])))
A=[pred[r['clip']][f"{r['f']} {r['c']} {r['fb']}"] for r in rows if not r['in_set']]
print('non-set rows %d: mean pred %+.2f'%(len(A),np.mean(A) if A else 0))
