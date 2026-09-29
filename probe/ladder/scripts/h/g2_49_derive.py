import json,math,statistics as st
s=json.load(open('/tmp/ladder_g/g2_scores.json'))
def adj(r,x,y):
    sl=(r['base144']['mos']-r['base112']['mos'])/math.log2(r['base144']['bytes']/r['base112']['bytes'])
    return (r[x]['mos']-r[y]['mos'])-sl*math.log2(r[x]['bytes']/r[y]['bytes'])
rows={}
for k,r in s.items():
    rows[k]=dict(stem=r['stem'][:22],AF=adj(r,'A','F'),fSF=adj(r,'fSF','A'),fWIN=adj(r,'fWIN','A'),rSF=adj(r,'rSF','F'),rWIN=adj(r,'rWIN','F'),AvApple=adj(r,'A','apple'),AppleF=adj(r,'apple','base128'))
for a in ('AF','fSF','fWIN','rSF','rWIN','AvApple','AppleF'):
    v=[x[a] for x in rows.values()]
    print(f"{a:8s} n={len(v)} mean {st.mean(v):+.4f} med {st.median(v):+.4f} win {sum(x>0 for x in v)} loss {sum(x<0 for x in v)}")
gap=sum(x['AF'] for x in rows.values())
for a in ('rSF','rWIN'): print(a,'share of A-F', 100*sum(x[a] for x in rows.values())/gap)
for a in ('fSF','fWIN'): print(a,'share of A-F', -100*sum(x[a] for x in rows.values())/gap)
print()
print(f"{'clip':22s} {'A-F':>7s} {'fSF':>7s} {'fWIN':>7s} {'rSF':>7s} {'rWIN':>7s}")
for k,x in sorted(rows.items(),key=lambda t:-t[1]['AF']):
    print(f"{x['stem']:22s} {x['AF']:+.3f} {x['fSF']:+.3f} {x['fWIN']:+.3f} {x['rSF']:+.3f} {x['rWIN']:+.3f}")
