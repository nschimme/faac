import sys,collections
# per-frame B records joined with window sequence from C records (first ICS)
f=sys.argv[1]; win={}; B={}
for l in open(f):
    p=l.split()
    if p[0]=='C' and int(p[1]) not in win: win[int(p[1])]=int(p[4])
    elif p[0]=='B': B[int(p[1])]=list(map(int,p[2:]))
names='total hdr sect sf spec aux sbr ps fill'.split()
agg=collections.defaultdict(lambda:[0]*9); n=collections.Counter()
for fr,b in B.items():
    k='short' if win.get(fr)==2 else 'long'
    n[k]+=1
    for i,v in enumerate(b): agg[k][i]+=v
    n['all']+=1
    for i,v in enumerate(b): agg['all'][i]+=v
for k in ('long','short','all'):
    if n[k]: print(f"{k:5} n={n[k]:4} "+' '.join(f"{nm}={agg[k][i]/n[k]:.0f}" for i,nm in enumerate(names)))
