"""B3 features: per band of the 0-6 kHz rSFr set (regular books 1-11 in both channels of FAAC and Apple, same window
layout), y = Apple sf - FAAC sf and within-frame features from FAAC's own normal encode (FAAC_G_MASK_DUMP: target,
band energy sum, peak energy; key (FAAC dump frame - 1, ch, group, sfb)). Also rows for every FAAC-regular band
< 6 kHz (in_set=0) so a fitted rule can be applied without Apple.
Control: the mask-dump encode decodes PCM-identical to <k>_normal.m4a.
usage: b3_feat.py <G> <rate>   env FAAC_BIN + base knobs.  Writes <G>/b3_rows.json"""
import os,sys,json,math,pathlib,subprocess,hashlib
here=pathlib.Path(__file__).resolve();sys.path.insert(0,str(here.parents[2]));sys.path.insert(0,str(here.parents[1]/'g'))
from parse_dump import parse
from g_make import fs
from line_level import long_off,short_off
G=pathlib.Path(sys.argv[1]);rate=sys.argv[2];faac=os.environ['FAAC_BIN']
def lay(v):return (v.win_seq,v.window_shape,v.num_groups,tuple(v.group_len))
def pcmh(m):return hashlib.md5(subprocess.run(['ffmpeg','-v','error','-i',str(m),'-f','s16le','-'],capture_output=True,check=True).stdout).hexdigest()
rows=[];ctl={}
for it in json.load(open(G/'g2_index.json')):
    k=it['id'];md=G/f'{k}_mask.txt';md.unlink(missing_ok=True);o=G/f'{k}_mask.m4a'
    subprocess.run([faac,'--overwrite','-b',rate,'-o',str(o),str(G/f'{k}_plus.wav')],env=dict(os.environ,FAAC_G_MASK_DUMP=str(md)),capture_output=True,check=True)
    ctl[k]=pcmh(o)==pcmh(G/f'{k}_normal.m4a');o.unlink()
    mask={}
    for line in open(md):
        t=line.split();mask[tuple(map(int,t[1:5]))]=tuple(map(float,t[5:8]))   # last write wins (final rate-loop pass)
    md.unlink()
    a=parse(str(G/f'{k}_apple.dump'));f=parse(str(G/f'{k}_normal.dump'));n=0
    for idx in range(max(f)+1):
        ff=f.get(idx,{});aa=a.get(idx+1,{})
        if not all(c in ff and ff[c].present for c in (0,1)):continue
        both=all(c in aa and aa[c].present for c in (0,1)) and all(lay(aa[c])==lay(ff[c]) for c in (0,1))
        fv=[ff[0],ff[1]];short=fv[0].win_seq==2
        for c in (0,1):
            v=fv[c];ics=[]
            for g in range(v.num_groups):
                gl=v.group_len[g] if short else 1
                for sb in range(min(fv[0].max_sfb,fv[1].max_sfb)):
                    fb=g*v.max_sfb+sb;hz=fs(v,sb)
                    if hz>=6000 or not all(1<=x.band_cb[fb]<=11 for x in fv):continue
                    m=mask.get((idx-1,c,g,sb))
                    if not m or m[1]<=0:continue
                    off=short_off if short else long_off
                    w=(off[sb+1]-off[sb])*gl;e=m[1]/w
                    inset=0;y=None
                    if both and sb<min(aa[0].max_sfb,aa[1].max_sfb):
                        ab=g*aa[0].max_sfb+sb
                        if all(1<=aa[x].band_cb[ab]<=11 for x in (0,1)):inset=1;y=aa[c].band_sf[ab]-v.band_sf[fb]
                    nb=[v.band_sf[g*v.max_sfb+s] for s in (sb-1,sb+1) if 0<=s<v.max_sfb and 1<=v.band_cb[g*v.max_sfb+s]<=11]
                    ics.append(dict(clip=k,f=idx,c=c,g=g,sb=sb,fb=fb,short=int(short),hz=hz,in_set=inset,y=y,sf=v.band_sf[fb],
                        lE=10*math.log10(e),tonal=10*math.log10(m[2]/e) if m[2]>0 else 0.0,lw=math.log10(w),
                        smr=10*math.log10(m[0]/e) if m[0]>0 else -99.0,nres=(v.band_sf[fb]-sum(nb)/len(nb)) if nb else 0.0,
                        ms=int(v.band_ms[fb]),gg=v.global_gain))
            if ics:
                mE=sum(r['lE'] for r in ics)/len(ics);msf=sum(r['sf'] for r in ics)/len(ics)
                for r in ics:r['relE']=r['lE']-mE;r['mE']=mE;r['relsf']=r['sf']-msf;r['nb06']=len(ics)
                rows+=ics;n+=sum(r['in_set'] for r in ics)
    print(k,it['stem'][:24],'ctl',ctl[k],'set06',n,flush=True)
json.dump(rows,open(G/'b3_rows.json','w'))
print('mask-dump control PCM-identical',sum(ctl.values()),'/',len(ctl))
