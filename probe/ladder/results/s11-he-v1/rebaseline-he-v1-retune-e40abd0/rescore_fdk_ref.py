import sys,os,json,subprocess
from multiprocessing import Pool
S='/tmp/claude-0/-home-user-faac/bc9c8615-9277-5565-96c9-f1ff39d121b8/scratchpad'
sys.path.insert(0,S+'/fb'); from phase2_mos import score_wav_pair
AUD=S+'/fb/data/external/audio'; AP='/home/user/faac/probe/ladder/ref/apple-s11-367cf8dc/he'
def enc_path(arm,clip,rate):
    return {'ret':f'{S}/run/out_ret/ret/{rate}/{clip}.m4a','fdk':f'{S}/run/out/fdk/{rate}/{clip}.m4a','apple':f'{AP}/{rate}k/{clip}.m4a','faac':f'{S}/run/out/faac/{rate}/{clip}.m4a'}[arm]
def job(a):
    arm,clip,rate=a; dec=f'/tmp/fr_{os.getpid()}.wav'
    r=subprocess.run([S+'/fb/bin/fdkdec',enc_path(arm,clip,rate),dec],capture_output=True)
    if r.returncode: return dict(arm=arm,clip=clip,rate=rate,mos_fdk=None)
    m,_=score_wav_pair(f'{AUD}/{clip}',dec,mode_str='audio',sample_rate=48000); os.remove(dec)
    return dict(arm=arm,clip=clip,rate=rate,mos_fdk=m)
if __name__=='__main__':
    clips=sorted(c for c in os.listdir(AUD) if c.endswith('.wav') and os.path.exists(f'{AP}/40k/{c}.m4a'))
    jobs=[(a,c,r) for a in sys.argv[1].split(',') for c in clips for r in (40,48,56,64)]
    with Pool(4) as p: res=p.map(job,jobs,chunksize=8)
    json.dump(res,open('fdkdec_ref_'+sys.argv[1].replace(',','_')+'.json','w'))
