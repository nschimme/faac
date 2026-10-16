import sys; sys.path.insert(0,'/tmp/claude')
from feat import *
L12=np.load('/tmp/claude/book12len.npy')
def dbits(d): return L12[np.clip(d,-60,60)+60]
def sfbits(a,idx):
    """delta bits for policy idx (array of candidate indices 0..4) chaining consecutive rows"""
    off=idx-2
    absf=a[:,11]+100-(a[:,9]+off)
    prev=np.roll(absf,1)
    cont=np.zeros(len(a),bool); cont[1:]=(a[1:,1]>a[:-1,1])&(a[1:,0]==a[:-1,0])&(a[1:,2]>0)
    # new group/call start: seed from f16 (p_last_abs) if set
    seed=a[:,16]; unset=seed<-1e9
    ref=np.where(cont,prev,np.where(unset,absf,seed))
    return dbits((absf-ref).astype(int)).sum()
def pt(a,idx):
    ix=np.arange(len(a)); return a[ix,35+idx].sum()+sfbits(a,idx), a[ix,40+idx].sum()
def curve(a): return [pt(a,np.full(len(a),i)) for i in (1,2,3)]
def gain(a,idx):
    B,D=pt(a,idx); cv=curve(a); bs=[c[0] for c in cv]; ds=[c[1] for c in cv]
    Dc=np.interp(B,bs,ds); return (Dc-D)/Dc*100, B/bs[1]
