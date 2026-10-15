import sys,os,subprocess,json,numpy as np,soundfile as sf
from scipy.signal import butter,sosfiltfilt
sys.path.insert(0,'../fb'); from phase2_mos import score_wav_pair
S='/tmp/claude-0/-home-user-faac/bc9c8615-9277-5565-96c9-f1ff39d121b8/scratchpad'
APPLE='/home/user/faac/probe/ladder/ref/apple-s11-367cf8dc/he'
BANDS=[('0-4k',None,4000),('4-8k',4000,8000),('8-12k',8000,12000),('12k+',12000,None)]
def filt(x,lo,hi):
    if lo is None: sos=butter(8,hi,'lp',fs=48000,output='sos')
    elif hi is None: sos=butter(8,lo,'hp',fs=48000,output='sos')
    else: sos=butter(8,[lo,hi],'bp',fs=48000,output='sos')
    return sosfiltfilt(sos,x,axis=0)
res={}
for clip in ['cst.wav','Girl_In_The_Fire__Sample_.16b48k.wav','35_SQAM_glockenspiel_cut.16b48k.wav','SinceAlways.16b48k.wav']:
    ref=f'{S}/fb/data/external/audio/{clip}'
    for rate in (48,64):
        for arm in ('faac','fdk','apple'):
            enc=f'{APPLE}/{rate}k/{clip}.m4a' if arm=='apple' else f'{S}/run/out/{arm}/{rate}/{clip}.m4a'
            subprocess.run([S+'/fb/bin/faad','-q','-o','/tmp/d.wav',enc],check=True,capture_output=True)
            r,_=sf.read(ref,dtype='float32',always_2d=True); d,_=sf.read('/tmp/d.wav',dtype='float32',always_2d=True)
            # align by bounded xcorr as scorer does: reuse by trimming equal length (gapless decode -> lag ~0)
            n=min(len(r),len(d)); r=r[:n]; d=d[:n]
            out={}
            for name,lo,hi in BANDS:
                sf.write('/tmp/r.wav',filt(r,lo,hi),48000,subtype='FLOAT'); sf.write('/tmp/dd.wav',filt(d,lo,hi),48000,subtype='FLOAT')
                m,_=score_wav_pair('/tmp/r.wav','/tmp/dd.wav',mode_str='audio',sample_rate=48000); out[name]=round(m,3)
            res[f'{clip}|{rate}|{arm}']=out; print(clip[:14],rate,arm,out,flush=True)
json.dump(res,open('bands.json','w'),indent=1)
