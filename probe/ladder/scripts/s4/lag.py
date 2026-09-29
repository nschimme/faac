"""Lag (samples) of a decoded stream against its source, by cross-correlation of the first 10 s. usage: lag.py src.wav enc.m4a"""
import sys,subprocess,numpy as np,soundfile as sf
x,_=sf.read(sys.argv[1]);x=x.mean(1)[:480000]
y=np.frombuffer(subprocess.run(['ffmpeg','-v','error','-i',sys.argv[2],'-f','f32le','-ac','1','-'],capture_output=True,check=True).stdout,'<f4')
n=1<<21;X=np.fft.rfft(x,n);Y=np.fft.rfft(y[:480000+8192],n);c=np.fft.irfft(Y*np.conj(X),n)
c=np.concatenate([c[-8192:],c[:8192]]);print(sys.argv[2].split('/')[-1],'lag',int(np.argmax(c))-8192,'len',len(y),'src',len(sf.read(sys.argv[1])[0]))
