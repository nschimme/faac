"""Energy (dB) per 1 kHz band of decoded streams vs the source. usage: bandE.py src.wav a.m4a [b.m4a ...]"""
import sys,subprocess,numpy as np,soundfile as sf
def spec(x):
    x=x[:len(x)//2048*2048].reshape(-1,2048)*np.hanning(2048);P=(np.abs(np.fft.rfft(x,axis=1))**2).mean(0)
    return [10*np.log10(P[int(k*2048/48):int((k+1)*2048/48)].sum()+1e-12) for k in range(24)]
s,_=sf.read(sys.argv[1]);r=spec(s.mean(1))
print('kHz   '+' '.join(f'{k:5d}' for k in range(24)));print('src   '+' '.join(f'{v:5.0f}' for v in r))
for m in sys.argv[2:]:
    y=np.frombuffer(subprocess.run(['ffmpeg','-v','error','-i',m,'-f','f32le','-ac','1','-'],capture_output=True,check=True).stdout,'<f4')
    print(f'{m[:5]:5s} '+' '.join(f'{a-b:+5.0f}' for a,b in zip(spec(y),r)))
