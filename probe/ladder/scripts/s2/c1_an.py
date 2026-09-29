import json,glob,math,numpy as np,collections
O='/home/user/wsC/c1';R=2.5;SM=0.3
lab=json.load(open('/home/user/wsC/clip_labels.json'))
GOOD=['velvet','bas','Changes','Shinsho','take_your','mof','trumpet'];BAD=['girl.16','Last_Of']
def load(k):
    rows=collections.defaultdict(dict)
    for l in open(f'{O}/{k}.bs'):
        f=l.split();fr,ch=int(f[1]),int(f[2]);rows[fr][ch]=dict(psy=int(f[3]),fin=int(f[5]),eng=list(map(float,f[8:32])))
    ap={int(a):b for a,b in json.load(open(f'{O}/{k}.apple.json'))['apple'].items()}
    return rows,ap
def feats(rows):
    fr=sorted(rows);chs=sorted(rows[fr[0]]);out={}
    per={}
    for ch in chs:
        e={}
        for f in fr:
            for j in range(16,24):e[8*f+j]=rows[f][ch]['eng'][j]
        lvl=0.0;r={}
        for g in sorted(e):
            r[g]=(e[g]+1e-9)/(lvl+1e-9);lvl=SM*e[g]+(1-SM)*lvl
        per[ch]=(e,r)
    for f in fr:
        rise=drop=0;loud=0
        for ch in chs:
            e,r=per[ch];gs=[g for g in range(8*f+7,8*f+18) if g in r]
            if not gs:continue
            fm=np.mean([e.get(g,0) for g in range(8*f+8,8*f+16)])+1e-9
            for g in gs:
                if r[g]>rise:rise=r[g];loud=e[g]/fm
                drop=max(drop,1/r[g])
        out[f]=dict(rise=rise,drop=drop,loud=loud,fin=rows[f][chs[0]]['fin'],psy=max(rows[f][c]['psy'] for c in chs))
    return out
S=lambda w:w==2
tot=collections.Counter();byclass=collections.defaultdict(list)
for fn in sorted(glob.glob(O+'/*.bs')):
    k=fn.split('/')[-1][:-3];rows,ap=load(k);F=feats(rows);st=lab[k]['stem']
    agree={o:np.mean([S(F[f]['fin'])==S(ap[f+o]) for f in F if f+o in ap]) for o in (0,1,2)}
    o=max(agree,key=agree.get);tot[o]+=1
    cls='good' if any(st.startswith(g) for g in GOOD) else 'bad' if any(st.startswith(b) for b in BAD) else 'other'
    for f,x in F.items():
        if f+2 in ap and f>2:byclass[cls].append((x["rise"],x["drop"],x["loud"],S(x["fin"]),x["psy"]==2,S(ap[f+2]),st))
print('best offset counts',dict(tot))
import sys
for cls,v in byclass.items():
    a=np.array([t[:6] for t in v],float)
    fs,as_=a[:,3]>0,a[:,5]>0
    print(f'== {cls}: frames {len(a)}  FAACshort {fs.mean():.2f} Appleshort {as_.mean():.2f}  FS&AL {(fs&~as_).mean():.2f} FS&AS {(fs&as_).mean():.2f} FL&AS {(~fs&as_).mean():.2f}')
    for nm,col in (('rise',0),('drop',1),('loud',2)):
        for sub,m in (('FS&AL',fs&~as_),('FS&AS',fs&as_),('FL&AL',~fs&~as_)):
            x=a[m,col]
            if len(x):print(f'   {nm:5s} {sub}: n={len(x):6d} q10/50/90 = {np.percentile(x,10):8.2f} {np.percentile(x,50):8.2f} {np.percentile(x,90):8.2f}')
