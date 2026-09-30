"""S6 core/SBR split helpers. Streams are handled as raw AU lists (ffmpeg ADTS copy); every arm is written as raw ADTS
at the 24 kHz core rate and decoded by the FAAD dump decoder (implicit SBR, 48 kHz out)."""
import os,sys,subprocess,pathlib,numpy as np,soundfile as sf
here=pathlib.Path(__file__).resolve();sys.path.insert(0,str(here.parents[1]/'s3'))
from he_splice import raw_aus,cpe_bits,splice,adts,tail_bits
FAAD='/tmp/faad-ladder-dump/build_faad/frontend/faad'
def write_adts(aus,path):open(path,'wb').write(b''.join(adts(a,24000) for a in aus))
def core_bits(aus,tmp):
    """per-AU CPE bit length (index 0-based) from a FAAD_LADDER_DUMP of the stream."""
    a=pathlib.Path(tmp);write_adts(aus,a);d=a.with_suffix('.dump');d.unlink(missing_ok=True)
    subprocess.run([FAAD,'-q','-f','raw','-o',str(a.with_suffix('.raw')),str(a)],env=dict(os.environ,FAAD_DUMP=str(d),FAAD_LADDER_DUMP='1'),capture_output=True,check=True)
    b=cpe_bits(d);[p.unlink(missing_ok=True) for p in (a,d,a.with_suffix('.raw'))]
    return {i-1:v for i,v in b.items()}
def decode(aus,wav):
    a=pathlib.Path(str(wav)+'.adts');write_adts(aus,a)
    subprocess.run([FAAD,'-q','-o',str(wav),str(a)],capture_output=True,check=True);a.unlink()
def lag(src,wav):
    """output-sample lag of a decoded stream vs its source (low band < 4 kHz, first 10 s, xcorr)."""
    import scipy.signal as ss
    x,_=sf.read(src);y,_=sf.read(wav);b,a=ss.butter(6,4000/24000)
    x=ss.filtfilt(b,a,x.mean(1)[:480000]);y=ss.filtfilt(b,a,y.mean(1)[:480000+16384])
    n=1<<21;c=np.fft.irfft(np.fft.rfft(y,n)*np.conj(np.fft.rfft(x,n)),n)[:16384]
    return int(np.argmax(c))
def mix(core,cb,sbr,sb,k):
    """output in `sbr`'s framing: frame j = core AU j-k (CPE) + sbr AU j tail; j-k outside core -> sbr AU as is."""
    out=[]
    for j in range(len(sbr)):
        i=j-k
        out.append(splice(core[i],cb[i],sbr[j],sb[j]) if 0<=i<len(core) and i in cb and j in sb else sbr[j])
    return out
