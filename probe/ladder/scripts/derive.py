from score_common import *
import math,collections
base={};armmos={};lines={}
for l in (root/'e2_score.log').read_text().splitlines():
 p=l.split();base[(p[0],p[1])]=(float(p[2]),int(p[3]))
for l in (root/'e3_score.log').read_text().splitlines():
 p=l.split();armmos[(p[0],p[1],p[2])]=(float(p[3]),int(p[4]))
for ref in ('apple','fdk'):
 print('\nREF',ref)
 for name in clips:
  b112=base[(name,'faac112')];b128=base[(name,'faac128')];b144=base[(name,'faac144')]
  slope=(b144[0]-b112[0])/math.log2(b144[1]/b112[1]);
  def adj(m,b,refm,refb):return m-refm-slope*math.log2(b/refb)
  if ref=='apple':
   old=armmos[(name,ref,'old_step1')];step=(base[(name,'apple_aligned_step1')][0],(root/(name+'_aligned_step1.m4a')).stat().st_size);a=base[(name,'apple_ref')]
   print('E2',name,'slope',round(slope,5),'ref',a[0],'step',step,'old',old,'faac',b128,'adjref',round(adj(*a,*b128),4),'adjstep',round(adj(*step,*b128),4),'adjold',round(adj(*old,*b128),4))
  for arm in ('K0','K1','Z','S','ZS','M','LO','HI'):
   mos,bytes_=armmos[(name,ref,arm)];k1=armmos[(name,ref,'K1')];k0=armmos[(name,ref,'K0')]
   d=adj(mos,bytes_,*k1);gap=adj(*k0,*k1)
   import re
   log=(root/(name+'_'+ref+'_'+arm+'.merge.log')).read_text(); changed=int(re.search(r'lines changed \(vs step1\) (\d+)/',log).group(1))
   lines[(name,ref,arm)]=changed
   print('ARM',name,arm,mos,bytes_,round(d,5),round(100*d/gap,1) if gap else None,changed)
 print('AGG',ref)
 for arm in ('K0','K1','Z','S','ZS','M','LO','HI'):
  vals=[];delta=[];gap=[];bytes_sum=changed_sum=0
  for name in clips:
   slope=(base[(name,'faac144')][0]-base[(name,'faac112')][0])/math.log2(base[(name,'faac144')][1]/base[(name,'faac112')][1]);m,b=armmos[(name,ref,arm)];m1,b1=armmos[(name,ref,'K1')];m0,b0=armmos[(name,ref,'K0')];vals.append(m);bytes_sum+=b;changed_sum+=lines[(name,ref,arm)];delta.append(m-m1-slope*math.log2(b/b1));gap.append(m0-m1-slope*math.log2(b0/b1))
  print(arm,round(sum(vals)/5,4),bytes_sum,round(sum(delta)/5,4),round(100*sum(delta)/sum(gap),1),changed_sum)
