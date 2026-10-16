import numpy as np,glob
def load(pat):
    return np.concatenate([np.fromfile(f,dtype=np.float32).reshape(-1,48) for f in sorted(glob.glob(pat))])
def costs(a,lam=0.08):
    c=a[:,40:45].astype(np.float64)+lam*a[:,35:40]
    c[a[:,35:40]<0]=np.inf
    return c
