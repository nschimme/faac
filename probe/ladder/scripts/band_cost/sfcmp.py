import sys,collections
from bandstats import load
def prof(p):
    ics=load(p); acc=[collections.defaultdict(list) for _ in range(2)]
    for idx,i in enumerate(ics):
        if i['win']==2: continue
        for (g,b,s,e,cb,sf,sfb,spb,nz,sa,mq) in i['b']:
            if 1<=cb<=11: acc[idx%2][b].append((sf,nz/(e-s),spb/(e-s)))
    return acc
c=sys.argv[1]
fa=prof(c+'.faac.dump'); ap=prof(c+'.apple.dump')
print("band lo-hz  | ch0: faac sf, apple sf, d | ch1: faac sf, apple sf, d | n faac/apple (coded long)")
off=[0,4,8,12,16,20,24,28,32,36,40,44,48,52,56,60,64,68,72,76,80,88,96,104,112,120,128,140,152,164,176,188,200,212,224,236,248,260,272,284,296,308,320,332,344,356,368,384,400,416,432,448,464,480,496,512,528,544,560,576,592,608,624,640,656,672,688,704,720,736,752,768,784,800,816,832,848,864,880,896,912,928,944,960,976,992,1008,1024]
for b in range(0,49):
    row=[]
    for ch in (0,1):
        f=fa[ch].get(b,[]); a=ap[ch].get(b,[])
        if not f or not a: row.append(None); continue
        mf=sum(x[0] for x in f)/len(f); ma=sum(x[0] for x in a)/len(a)
        row.append((mf,ma,mf-ma,len(f),len(a)))
    if row[0] is None and row[1] is None: continue
    s=f"{b:3d}"
    for r in row: s+= "  |  "+("   -   " if r is None else f"{r[0]:6.1f} {r[1]:6.1f} {r[2]:+5.1f} ({r[3]}/{r[4]})")
    print(s)
