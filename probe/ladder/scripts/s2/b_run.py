# B2: step1 on the #595 encoder's own decisions, rewriting long-block sf above 6 kHz only.
import sys,os,copy,json,re,subprocess,numpy as np,soundfile as sf
sys.path.insert(0,'/home/user/faac/probe/ladder')
from parse_dump import parse,ICS
from line_level import long_off
G='/home/user/ladder_work_m/g';W='/home/user/wsB/b2';os.makedirs(W,exist_ok=True)
F='/home/user/faac/build_ladder/frontend/faac';SC=['/opt/venv312/bin/python3','/opt/faac-benchmark/scripts/score_clip.py']
ARMS=dict(K0={},H4={2:0.4,3:0.4},H7={2:0.7,3:0.7},H10={2:1.0,3:1.0});OFF=dict(L1={0:1},L2={0:2},Hm1={2:-1,3:-1},L3={0:3},L4={0:4})
for a in OFF:ARMS[a]={}
arms=sys.argv[1].split(',') if len(sys.argv)>1 else list(ARMS)
idx=json.load(open(G+'/g2_index.json'));out=W+'/scores.json';res=json.load(open(out)) if os.path.exists(out) else {}
def reg(s):
    f=long_off[s]*24000/1024;return 0 if f<2000 else 1 if f<6000 else 2 if f<12000 else 3
def make(k,arm):
    n=parse(f'{G}/{k}_smooth.dump');al=ARMS[arm];nb=0
    with open(f'{W}/{k}_{arm}.bin','wb') as o,open(f'{W}/{k}_{arm}_origin.bin','wb') as og:
        for i in range(max(n)+1):
            for ch in (0,1):
                fv=n.get(i,{}).get(ch,ICS());x=copy.deepcopy(fv);org=ICS();org.present=x.present
                for b in range(128):org.band_cb[b]=1
                if arm in OFF and fv.present and fv.win_seq!=2:
                    for s_ in range(fv.max_sfb):
                        dv=OFF[arm].get(reg(s_),0)
                        if 1<=fv.band_cb[s_]<=11 and dv:x.band_sf[s_]=max(0,min(255,fv.band_sf[s_]+dv));nb+=1
                    prev=None
                    for s_ in range(fv.max_sfb):
                        if 1<=fv.band_cb[s_]<=11:
                            if prev is not None:x.band_sf[s_]=max(prev-60,min(prev+60,x.band_sf[s_]))
                            prev=x.band_sf[s_]
                if al and fv.present and fv.win_seq!=2:
                    coded=[1<=fv.band_cb[s]<=11 for s in range(fv.max_sfb)];s0=fv.band_sf
                    for s in range(fv.max_sfb):
                        a=al.get(reg(s),0)
                        if not coded[s] or not a:continue
                        nb1=[s0[t] for t in (s-1,s+1) if 0<=t<fv.max_sfb and coded[t]]
                        if not nb1:continue
                        new=max(0,min(255,int(round(s0[s]-a*(s0[s]-np.mean(nb1))))))
                        if new!=s0[s]:x.band_sf[s]=new;nb+=1
                    prev=None
                    for s in range(fv.max_sfb):
                        if coded[s]:
                            if prev is not None:x.band_sf[s]=max(prev-60,min(prev+60,x.band_sf[s]))
                            prev=x.band_sf[s]
                o.write(x.pack());og.write(org.pack())
    return nb
def pcm(m):
    r=W+'/tmp.f32';subprocess.run(['ffmpeg','-v','error','-y','-i',m,'-f','f32le','-ac','2',r],check=True);x=np.fromfile(r,'<f4').reshape(-1,2);os.remove(r);return x
for it in idx:
    k=it['id'];r=res.get(k,{})
    ref=None
    for arm in arms:
        if arm+'_s' in r:continue
        units=make(k,arm);enc=f'{W}/{k}_{arm}.m4a'
        env=dict(os.environ,FAAC_STEP1=f'{W}/{k}_{arm}.bin',FAAC_STEP1_OFFSET='1',FAAC_STEP1_ORIGIN=f'{W}/{k}_{arm}_origin.bin')
        subprocess.run([F,'--overwrite','-b','128','-o',enc,f'{G}/{k}_plus.wav'],env=env,capture_output=True,check=True)
        x=pcm(enc)
        if arm=='K0':
            r['K0']=bool(np.array_equal(x,pcm(f'{G}/{k}_smooth.m4a')));print(k,'K0',r['K0'],flush=True)
        n=sf.info(it['src']).frames;wav=f'{W}/{k}.wav';sf.write(wav,x[64:64+n],48000,subtype='FLOAT')
        p=subprocess.run(SC+[it['src'],wav],capture_output=True,text=True);m=re.search(r'MOS: ([\d.]+)',p.stdout)
        r[arm+'_s']={'mos':float(m.group(1)),'bytes':os.path.getsize(enc),'units':units};print(k,arm,r[arm+'_s'],flush=True)
        os.remove(enc)
        res[k]=r;json.dump(res,open(out,'w'),indent=0)
print('DONE')
