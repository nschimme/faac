from load import *
def feats(a):
    w=a[:,2]; g=a[:,3]; n=w*g
    tgt=a[:,6]; avg=a[:,7]; pk=a[:,8]; sf=a[:,9]; al=a[:,14]; a2=a[:,15]
    gap=a2-al                       # band energy over allowed noise (dB)
    lq=0.75*(pk/6.0206+sf/4.0)      # log2 est of qmax at formula sf
    pa=pk-(a2-10*np.log10(n))       # peak vs per-line avg (dB)
    F=dict(gap=gap,lq=lq,pa=pa,w=w,g=g,sb=a[:,1],bt=a[:,0],rate=a[:,4]/1000,
           dn=np.where(a[:,12]>90,0,a[:,12]),dp=np.where(a[:,13]>90,0,a[:,13]),
           tg_av=tgt-avg, avgdb=avg, prev=np.where(a[:,18]>90,0,a[:,18]),
           a2n=a2-10*np.log10(n), alw=al-10*np.log10(n))
    F.update(q0=a[:,22],nz0=a[:,27],cm0=a[:,32],b0=a[:,37],d0=a[:,42],bpl=a[:,37]/n,nzf=a[:,27]/n,lg_d0=np.log10(a[:,42]+1e-6),prevq=0*a[:,0])
    return F
def X(a,names):
    F=feats(a); return np.stack([F[k] for k in names],1)
def regret(c,pred):
    ix=np.arange(len(c)); return (c[ix,pred]-c.min(1)).mean()
