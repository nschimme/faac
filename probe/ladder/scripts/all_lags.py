from score_common import *
import scipy.signal as ss
for name,stem in clips.items():
 src=sf.read(data/(stem+'.wav'),dtype='float32')[0][:,0]
 for label,aac in [('apple_step1',root/(name+'_aligned_step1.m4a')),('apple_own',root/(name+'_apple_own.aac'))]:
  raw=root/(name+'_'+label+'_lag.f32');run(['ffmpeg','-v','error','-y','-i',str(aac),'-f','f32le','-acodec','pcm_f32le','-ac','2','-ar','48000',str(raw)])
  x=np.fromfile(raw,dtype='<f4').reshape(-1,2)[:,0]
  a=src[10000:40000];b=x[5000:45000]
  lag=int(np.argmax(ss.correlate(b,a,mode='valid',method='fft')))+5000-10000
  print(name,label,lag,flush=True)
