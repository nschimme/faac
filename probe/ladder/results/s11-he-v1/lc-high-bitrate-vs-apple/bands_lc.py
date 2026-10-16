import sys,os,subprocess,json,random,numpy as np,soundfile as sf
from multiprocessing import Pool
from scipy.signal import butter,sosfiltfilt
S='/tmp/claude-0/-home-user-faac/bc9c8615-9277-5565-96c9-f1ff39d121b8/scratchpad'
sys.path.insert(0,S+'/fb'); from phase2_mos import score_wav_pair
AP='/home/user/faac/probe/ladder/ref/apple-s11-367cf8dc/lc'
BANDS=[('0-2k',None,2000),('2-6k',2000,6000),('6-12k',6000,12000),('12-16k',12000,16000),('16k+',16000,None)]
def filt(x,lo,hi):
    if lo is None: sos=butter(8,hi,'lp',fs=48000,output='sos')
    elif hi is None: sos=butter(8,lo,'hp',fs=48000,output='sos')
    else: sos=butter(8,[lo,hi],'bp',fs=48000,output='sos')
    return sosfiltfilt(sos,x,axis=0)
def enc(arm,clip,rate): return f'{AP}/{rate}k/{clip}.m4a' if arm=='apple' else f'{S}/run/out_lc/{arm}/{rate}/{clip}.m4a'
def job(a):
    clip,rate=a; ref=f'{S}/fb/data/external/audio/{clip}'; pid=os.getpid(); out={}
    r,_=sf.read(ref,dtype='float32',always_2d=True)
    for arm in ('faac','fdk','apple'):
        w=f'/tmp/bl_{pid}.wav'
        subprocess.run([S+'/fb/bin/faad','-q','-o',w,enc(arm,clip,rate)],capture_output=True)
        d,_=sf.read(w,dtype='float32',always_2d=True); n=min(len(r),len(d)); rr=r[:n]; dd=d[:n]
        res={}
        for name,lo,hi in BANDS:
            sf.write(f'/tmp/blr_{pid}.wav',filt(rr,lo,hi),48000,subtype='FLOAT'); sf.write(f'/tmp/bld_{pid}.wav',filt(dd,lo,hi),48000,subtype='FLOAT')
            res[name]=score_wav_pair(f'/tmp/blr_{pid}.wav',f'/tmp/bld_{pid}.wav',mode_str='audio',sample_rate=48000)[0]
        out[arm]=res
    return dict(clip=clip,rate=rate,bands=out)
if __name__=='__main__':
    rows=json.load(open('rows_lc_faac_fdk_apple.json')); M={(r['arm'],r['clip'],r['rate']):r['mos'] for r in rows}
    clips=sorted({r['clip'] for r in rows}); rate=int(sys.argv[1])
    worst=[c for _,c in sorted((M[('faac',c,rate)]-M[('apple',c,rate)],c) for c in clips)[:8]]
    random.seed(3); rest=[c for c in clips if c not in worst]; pick=worst+random.sample(rest,8)
    with Pool(2) as p: res=p.map(job,[(c,rate) for c in pick])
    json.dump(res,open(f'bands_lc_{rate}.json','w'))
    for grp,name in ((res[:8],'worst-8'),(res[8:],'random-8'),(res,'all-16')):
        print(name,rate)
        for b,_,_ in BANDS:
            f=np.mean([x['bands']['faac'][b] for x in grp]); a=np.mean([x['bands']['apple'][b] for x in grp]); k=np.mean([x['bands']['fdk'][b] for x in grp])
            print(f'  {b:7s} faac {f:.3f}  apple {a:.3f}  fdk {k:.3f}   faac-apple {f-a:+.3f}')
