from pareto import *
from sklearn.tree import DecisionTreeClassifier, DecisionTreeRegressor
tr=load('/tmp/claude/log/e_*.bin'); te=load('/tmp/claude/log/o_*.bin')
y=(tr[:,17]+2).astype(int); ctr=costs(tr)
base=['gap','lq','pa','w','g','bt','sb','dn','dp']
ext=base+['q0','nz0','cm0','b0','d0','bpl','nzf','lg_d0']
sp=np.minimum(ctr[:,1:4].max(1)-ctr[:,1:4].min(1),30)
for nm,names in (('base',base),('ext',ext)):
  for d in (4,8):
    for w in ('none','spread'):
        m=DecisionTreeClassifier(max_depth=d,min_samples_leaf=500).fit(X(tr,names),y,sample_weight=None if w=='none' else sp)
        p=m.predict(X(te,names)); g=gain(te,p); print(nm,d,w,'gain %.2f bits %.4f'%g, 'acc %.3f'%(p==(te[:,17]+2)).mean())
