import sys,os,json,subprocess
from multiprocessing import Pool
S='/tmp/claude-0/-home-user-faac/bc9c8615-9277-5565-96c9-f1ff39d121b8/scratchpad'
sys.path.insert(0,S+'/fb'); from phase2_mos import score_wav_pair
CFG={'48k':(S+'/fb/data/external/audio',S+'/run/out',(40,48,56,64)),
     '44k1':(S+'/fb/data/external/audio_44k1',S+'/run/out_44k1',(40,48,56,64)),
     '32k':(S+'/fb/data/external/audio_32k',S+'/run/out_32k',(32,40,48,56)),
     '32kb':(S+'/fb/data/external/audio_32k',S+'/run/out_32kb',(32,40,48,56)),
     'x2':(S+'/fb/data/external/audio',S+'/run/out_x2',(40,48,56,64)),
     'x244':(S+'/fb/data/external/audio_44k1',S+'/run/out_x244',(40,48,56,64)),
     'x232':(S+'/fb/data/external/audio_32k',S+'/run/out_x232',(32,40,48,56)),
     'held':(S+'/held',S+'/run/out_held',(40,48,56,64))}
def job(a):
    tag,arm,clip,rate=a; AUD,OUT,_=CFG[tag]; enc=f'{OUT}/{arm}/{rate}/{clip}.m4a'; dec=f'/tmp/fd_{os.getpid()}.wav'
    r=subprocess.run([S+'/fb/bin/fdkdec',enc,dec],capture_output=True)
    if r.returncode: return dict(tag=tag,arm=arm,clip=clip,rate=rate,mos_fdk=None)
    m,_=score_wav_pair(f'{AUD}/{clip}',dec,mode_str='audio',sample_rate=48000); os.remove(dec)
    return dict(tag=tag,arm=arm,clip=clip,rate=rate,mos_fdk=m)
if __name__=='__main__':
    jobs=[]
    for tag in sys.argv[1].split(','):
        AUD,OUT,R=CFG[tag]; clips=sorted(c for c in os.listdir(AUD) if c.endswith('.wav'))
        jobs+=[(tag,a,c,r) for a in (('fix15','fix13','fix14') if tag=='32kb' else (('x2c12','x2c13','x2c11','x2c10','x2c9','x2n12','x2n9') if tag=='x2' else (('x2c12','x2n12','x2n9','x2n13') if tag=='x244' else (('x2c12','x2n12','x2n13','x2n9') if tag=='x232' else None)) or ('fix15','fix11','fix9'))) for c in clips for r in R]
    with Pool(4) as p: res=p.map(job,jobs,chunksize=8)
    json.dump(res,open('fdkdec_'+sys.argv[1].replace(',','_')+'.json','w'))
