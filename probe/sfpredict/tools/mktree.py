import sys
from feat import *
from sklearn.tree import DecisionTreeClassifier, DecisionTreeRegressor
def export(m,names,out,reg=False):
    t=m.tree_; L=[]
    def rec(n,ind):
        sp='  '*ind
        if t.children_left[n]==-1:
            v=t.value[n][0]
            off=int(np.rint(v[0])) -2 if reg else int(m.classes_[np.argmax(v)])-2
            L.append(f'{sp}return {off};'); return
        L.append(f'{sp}if (f[{names.index(names[t.feature[n]])}] <= {t.threshold[n]:.4f}f) {{')
        rec(t.children_left[n],ind+1); L.append(f'{sp}}} else {{'); rec(t.children_right[n],ind+1); L.append(f'{sp}}}')
    rec(0,1)
    open(out,'w').write('/* generated */\n'+'\n'.join(L)+'\n')
if __name__=='__main__':
    depth=int(sys.argv[1]); leaf=int(sys.argv[2]); names=sys.argv[3].split(','); out=sys.argv[4]; reg=len(sys.argv)>5 and sys.argv[5]=='reg'
    tr=load('/tmp/claude/log/e_*.bin'); y=(tr[:,17]+2).astype(int)
    M=(DecisionTreeRegressor if reg else DecisionTreeClassifier)(max_depth=depth,min_samples_leaf=leaf).fit(X(tr,names),y)
    export(M,names,out,reg); print(names,M.get_n_leaves())
