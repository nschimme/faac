from score_common import *
for name,stem in clips.items():
 src=data/(stem+'.wav');n=sf.info(src).frames;ref=repo/'probe/ladder/ref/apple'/(stem+'.m4a');own=root/(name+'_apple_own.aac')
 rw=root/(name+'_apple_ref_score.wav');ow=root/(name+'_apple_own_score.wav');wav_from_aac(ref,rw,0,n)
 a=sf.read(rw,dtype='float32')[0];b=sf.read(ow,dtype='float32')[0];print(name,'PCM maxerr',float(np.max(np.abs(a-b))),'diffsamples',int(np.count_nonzero(a!=b)),'refWavMOS',score(src,rw),'ownMOS',score(src,ow),flush=True)
