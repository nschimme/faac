import json,sys,collections
def load(t):
    d={}
    for s,r,k,m in json.load(open(f'/tmp/claude/h/{t}.json')): d[(s,r)]=(k,m)
    return d
b=load(sys.argv[1]); c=load(sys.argv[2])
ks=[k for k in c if k in b]
for r in sorted({k[1] for k in ks}):
    kk=[k for k in ks if k[1]==r]
    dm=sum(c[k][1]-b[k][1] for k in kk)/len(kk); db=sum(c[k][0]/b[k][0]-1 for k in kk)/len(kk)*100
    print(r,len(kk),f"dMOS {dm:+.4f} dBR {db:+.2f}%  est BD ~ {db-dm/0.0016*1:+.2f}%" )
for k in sorted(ks): 
    if len(sys.argv)>3: print(k, round(c[k][1]-b[k][1],3), round((c[k][0]/b[k][0]-1)*100,2))
