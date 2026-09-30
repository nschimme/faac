"""B3 step1 arms from FAAC's own decisions (b2_make pattern; changed bands get origin 0).
  K0      = builder, no change (must equal KF, PCM)
  rSF06   = Apple sf on the 0-6 kHz rSFr set (ceiling)
  rFIT    = FAAC sf + cross-fitted prediction on the set
  rFITall = FAAC sf + prediction on every FAAC-regular band < 6 kHz (b3_rows), no Apple needed
  rFITallS<x> = rFITall with the prediction scaled by x (env B3_SCALES, e.g. "0.5,1.5")
usage: b3_make.py <G>   env B3_PRED (default b3_pred.json), B3_TAG (default FIT: arm names r<TAG>, r<TAG>all)"""
import os,sys,copy,json,pathlib
here=pathlib.Path(__file__).resolve();sys.path.insert(0,str(here.parents[2]))
from parse_dump import parse,ICS
G=pathlib.Path(sys.argv[1]);rows=json.load(open(G/'b3_rows.json'));pred=json.load(open(G/os.environ.get('B3_PRED','b3_pred.json')))
T=os.environ.get('B3_TAG','FIT')
SC=[float(s) for s in os.environ.get('B3_SCALES','').split(',') if s]
ARMS=(['K0','rSF06'] if T=='FIT' else [])+[f'r{T}',f'r{T}all']+[f'r{T}allS{s:g}' for s in SC]
by={}
for r in rows:by.setdefault(r['clip'],{}).setdefault((r['f'],r['c']),[]).append(r)
out={}
for it in json.load(open(G/'g2_index.json')):
    k=it['id'];f=parse(str(G/f'{k}_normal.dump'));a=parse(str(G/f'{k}_apple.dump'));R=by.get(k,{});P=pred.get(k,{})
    files={arm:(open(G/f'{k}_G_{arm}.bin','wb'),open(G/f'{k}_G_{arm}_origin.bin','wb')) for arm in ARMS};cnt={arm:0 for arm in ARMS}
    for idx in range(max(a)+1):
        ff=f.get(idx,{});aa=a.get(idx+1,{})
        for arm in ARMS:
            recs=[copy.deepcopy(ff.get(c,ICS())) for c in (0,1)];org=[ICS(),ICS()]
            for c in (0,1):org[c].present=recs[c].present;org[c].band_cb=[1]*128
            if arm!='K0':
                for c in (0,1):
                    for r in R.get((idx,c),()):
                        if arm in ('rSF06',f'r{T}') and not r['in_set']:continue
                        old=ff[c].band_sf[r['fb']];p=P[f"{idx} {c} {r['fb']}"]
                        if arm=='rSF06':new=old+r['y']
                        elif 'allS' in arm:new=old+round(float(arm.split('allS')[1])*p)
                        else:new=old+p
                        if new!=old:recs[c].band_sf[r['fb']]=new;org[c].band_cb[r['fb']]=0;cnt[arm]+=1
            fo,fg=files[arm]
            for c in (0,1):fo.write(recs[c].pack());fg.write(org[c].pack())
    for fo,fg in files.values():fo.close();fg.close()
    out[k]={'stem':it['stem'],'changed':cnt};print(k,it['stem'][:24],cnt,flush=True)
json.dump(out,open(G/('b3_make.json' if T=='FIT' else f'b3_make_{T}.json'),'w'),indent=0)
