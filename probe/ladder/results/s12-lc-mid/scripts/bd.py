import json,sys,statistics as st
sys.path.insert(0,'/opt/faac-benchmark/scripts'); sys.path.insert(0,'/opt/faac-benchmark')
from bd_rate import bd_rate_curve
GATE=['sandman.16b48k','velvet.16b48k','21-classic.441.16b48k','fms']
def curves(store_a, store_f, fams=('48k','44k1'), clips=None, rungs=None):
    out={}
    for key,ar in store_a.items():
        fam,n=key.split('|')
        if fam not in fams or (clips and n not in clips): continue
        fr=store_f.get(key,{})
        rs=sorted(set(map(int,ar))&set(map(int,fr)))
        if rungs: rs=[r for r in rs if r in rungs]
        if len(rs)<3: continue
        a=[(ar[str(r)][1],ar[str(r)][0]) for r in rs]; f=[(fr[str(r)][1],fr[str(r)][0]) for r in rs]
        out[key]=bd_rate_curve(a,f)
    return out
def report(label,store_f,clips=None,store_a=None):
    store_a=store_a or json.load(open('/home/user/work/an/store.json'))['apple']
    for fam in ('48k','44k1'):
        c=curves(store_a,store_f,(fam,),clips)
        v=[x for x in c.values() if x is not None]
        if v: print(f'{label:18s} {fam:5s} n={len(v):2d} mean BD {st.mean(v):+6.2f}%  median {st.median(v):+6.2f}%  worst {max(v):+6.2f}%  best {min(v):+6.2f}%')
    return
if __name__=='__main__':
    s=json.load(open('/home/user/work/an/store.json'))
    report('baseline GATE',s['faac'],GATE,s['apple'])
    report('baseline ALL49',s['faac'],None,s['apple'])
    c=curves(s['apple'],s['faac'],('48k',),GATE)
    for k,v in c.items(): print('  ',k,None if v is None else round(v,2))
    c=curves(s['apple'],s['faac'],('44k1',),GATE)
    for k,v in c.items(): print('  ',k,None if v is None else round(v,2))
