import json,collections
mos=collections.defaultdict(dict)   # (fam,rung,clip)->{faac,apple}
for f in ('mos_96_160_192.json','mos_128.json'):
    for t,n,e,v in json.load(open(f)): mos[('48k',int(t),n)][e]=v
for t,n,e,v in json.load(open('mos_extra.json')):
    fam,r=t.split('_'); mos[(fam,int(r[:-1]),n)][e]=v
sz={}
for l in open('sizes.txt'):
    t=l.split(); sz[('48k',int(t[0]),' '.join(t[1:-2]))]=(int(t[-2]),int(t[-1]))
for l in open('sizes2.txt'):
    t=l.split(); fam,r=t[0].split('_'); sz[(fam,int(r[:-1]),' '.join(t[1:-2]))]=(int(t[-2]),int(t[-1]))
store={'apple':{}, 'faac':{}}
for (fam,r,n),d in mos.items():
    f,a=sz[(fam,r,n)]
    store['apple'].setdefault(f'{fam}|{n}',{})[r]=(d['apple'],a)
    store['faac'].setdefault(f'{fam}|{n}',{})[r]=(d['faac'],f)
json.dump(store,open('store.json','w'))
print({k:len(v) for k,v in store['apple'].items().__iter__().__class__ and [(k,len(v)) for k,v in store['apple'].items()][:2]} if False else len(store['apple']))
