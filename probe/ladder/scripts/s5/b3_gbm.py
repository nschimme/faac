"""B3b: gradient-boosted fit of y on the B3 features, 2-fold by clip parity, cross-fitted; writes <G>/b3_pred_gbm.json.
usage: b3_gbm.py <G>"""
import sys,json,pathlib,collections,numpy as np
from sklearn.ensemble import HistGradientBoostingRegressor as H
G=pathlib.Path(sys.argv[1]);rows=json.load(open(G/'b3_rows.json'))
F='relE,mE,tonal,lw,smr,nres,relsf,hz,short,ms'.split(',')
X=np.array([[r[f] for f in F] for r in rows]);fold=np.array([int(r['clip'].split('_')[-1])%2 for r in rows]);ins=np.array([r['in_set'] for r in rows],bool)
y=np.array([r['y'] if r['in_set'] else 0 for r in rows],float);p=np.zeros(len(rows))
for k in (0,1):
    tr=ins&(fold!=k);m=H(max_iter=200).fit(X[tr],y[tr]);p[fold==k]=m.predict(X[fold==k])
    te=ins&(fold==k);print(f'fold {k} test R2 {1-np.sum((y[te]-p[te])**2)/np.sum((y[te]-y[te].mean())**2):.3f}')
pred=collections.defaultdict(dict)
for r,v in zip(rows,p):pred[r['clip']][f"{r['f']} {r['c']} {r['fb']}"]=int(np.rint(v))
json.dump(pred,open(G/'b3_pred_gbm.json','w'))
P=np.rint(p[ins]);print('set exact %.1f%% |err|<=1 %.1f%%'%(100*np.mean(P==y[ins]),100*np.mean(np.abs(P-y[ins])<=1)))
