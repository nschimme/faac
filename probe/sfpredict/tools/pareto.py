import sys; sys.path.insert(0,'/tmp/claude')
from feat import *
def pt(a,idx):
    ix=np.arange(len(a)); return a[ix,35+idx].sum(), a[ix,40+idx].sum()
def curve(a):
    # constants -1,0,+1 => idx 1,2,3
    return [pt(a,np.full(len(a),i)) for i in (1,2,3)]
def gain(a,idx):
    B,D=pt(a,idx); cv=curve(a); bs=[c[0] for c in cv]; ds=[c[1] for c in cv]
    # bits increase with idx; interpolate D at B
    Dc=np.interp(B,bs,ds); return (Dc-D)/Dc*100, B/bs[1]
if __name__=='__main__':
    te=load('/tmp/claude/log/o_*.bin')
    lab=(te[:,17]+2).astype(int)
    print('search',gain(te,lab))
    print('curve',curve(te))
