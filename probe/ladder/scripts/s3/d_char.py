"""D characterisation: where Apple codes M/S and FAAC (#595+#599 base) does not.
Long blocks, both encoders long, band regular (books 1-11) in both channels of both streams.
FAAC stereo features from FAAC_MS_DUMP (ms frame f = FAAC dump f+1 = Apple dump f+2)."""
import sys,json,math,glob,os,collections,pickle
sys.path.insert(0,os.path.dirname(__file__));sys.path.insert(0,os.path.join(os.path.dirname(__file__),'../..'))
from parse_dump import parse
from ms_load import load_ms
from line_level import long_off
G=sys.argv[1];M=sys.argv[2];rows=[]
for it in json.load(open(G+'/g2_index.json')):
    k=it['id'];ms=load_ms(f'{M}/{k}.ms');n=parse(f'{G}/{k}_normal.dump');a=parse(f'{G}/{k}_apple.dump')
    for f,rec in ms.items():
        x=n.get(f+1,{});y=a.get(f+2,{})
        if not (0 in x and 1 in x and 0 in y and 1 in y):continue
        if any(z.win_seq==2 for z in (x[0],x[1],y[0],y[1])) or not (x[0].present and y[0].present):continue
        for (g,sfb),v in rec.items():
            if sfb>=min(x[0].max_sfb,y[0].max_sfb):continue
            cls=[x[0].band_cb[sfb],x[1].band_cb[sfb],y[0].band_cb[sfb],y[1].band_cb[sfb]]
            if not all(1<=c<=11 for c in cls):continue
            sh,el,er,elr,w,u=v
            if el<=0 or er<=0:continue
            rows.append((it['stem'],sfb,(long_off[sfb]+long_off[sfb+1])*12/1024,el,er,elr,u,x[0].band_ms[sfb],y[0].band_ms[sfb],
                         x[0].band_sf[sfb],x[1].band_sf[sfb],y[0].band_sf[sfb],y[1].band_sf[sfb]))
pickle.dump(rows,open(sys.argv[3],'wb'));print(len(rows),'bands')
