import sys, runpy, tempfile, json
from pathlib import Path
bench=Path('/Users/nschimme/gitprojects/faac-benchmark')
sys.path.insert(0,str(bench))
from codec_bench.encoders import FAACEncoder
ns=runpy.run_path('/private/tmp/faac-he-v1-probes/probe/run_features.py',run_name='identity')
src=bench/'data/external/audio/sandman.16b48k.wav'
bins=[('base',Path('/private/tmp/faac-he-v1-probes/build-baseline/frontend/faac')),('noise',Path('/private/tmp/faac-he-v1-adaptive/build-probe/frontend/faac')),('invf',Path('/private/tmp/faac-he-v1-invf/build-probe/frontend/faac')),('noise18',Path('/private/tmp/faac-he-v1-noise/build-candidate/frontend/faac')),('invf18',Path('/private/tmp/faac-he-v1-invf/build-probe/frontend/faac'))]
td=tempfile.TemporaryDirectory(prefix='faac-invf-identity-'); root=Path(td.name); rows=[]
for label,binary in bins:
 for rate in (32,48):
  env={} if label not in ('noise18','invf18') else {'FAAC_SBR_STATIC_NOISE_LEVEL':'18'}
  out=root/(label+str(rate)+'.m4a')
  ns['encode'](binary,src,out,rate,env,FAACEncoder)
  rows.append({'label':label,'rate':rate,'sha256':ns['payload_sha256'](out)})
print(json.dumps(rows,sort_keys=True)); td.cleanup()
