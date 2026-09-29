"""KA integer match: dump <G>/<k>_G_A.m4a and compare quantized integers with the Apple dump (A frame i = Apple i+1),
same layout ICS, lines below `limit` (core line index; 512 = Apple HE's 6 kHz crossover; 1024 = all).
usage: ka_match.py <G> [limit]"""
import sys,os,json,pathlib,subprocess,numpy as np
here=pathlib.Path(__file__).resolve();sys.path.insert(0,str(here.parents[2]))
from parse_dump import parse
G=pathlib.Path(sys.argv[1]);lim=int(sys.argv[2]) if len(sys.argv)>2 else 1024;faad='/tmp/faad-ladder-dump/build_faad/frontend/faad'
S={'long':[0,0],'short':[0,0]};res={}
for it in json.load(open(G/'g2_index.json')):
    k=it['id'];d=G/f'{k}_G_A.dump';d.unlink(missing_ok=True)
    subprocess.run([faad,'-q','-o',str(G/'t.wav'),str(G/f'{k}_G_A.m4a')],env=dict(os.environ,FAAD_DUMP=str(d),FAAD_LADDER_DUMP='1'),capture_output=True,check=True)
    f=parse(str(d));a=parse(str(G/f'{k}_apple.dump'));s=t=0
    for i in f:
        if i+1 not in a:continue
        for c in (0,1):
            x=f[i].get(c);y=a[i+1].get(c)
            if not x or not y or not x.present or not y.present or (x.win_seq,x.num_groups,x.max_sfb)!=(y.win_seq,y.num_groups,y.max_sfb):continue
            q1=np.array(x.quantized).reshape(-1,128) if x.win_seq==2 else np.array(x.quantized)[None]
            q2=np.array(y.quantized).reshape(-1,128) if y.win_seq==2 else np.array(y.quantized)[None]
            L=lim//8 if x.win_seq==2 else lim;q1=q1[:,:L];q2=q2[:,:L];m=(q1!=0)|(q2!=0)
            key='short' if x.win_seq==2 else 'long';S[key][0]+=int((q1[m]==q2[m]).sum());S[key][1]+=int(m.sum());s+=int((q1[m]==q2[m]).sum());t+=int(m.sum())
    res[it['stem']]=s/max(t,1);d.unlink()
v=sorted(res.values());print('limit',lim,'per-clip match min/median/max',round(v[0],4),round(v[len(v)//2],4),round(v[-1],4),
 {k:round(a/max(b,1),4) for k,(a,b) in S.items()},'clips >=0.97:',sum(x>=0.97 for x in v),'/',len(v))
json.dump(res,open(G/f'ka_match_{lim}.json','w'),indent=0)
