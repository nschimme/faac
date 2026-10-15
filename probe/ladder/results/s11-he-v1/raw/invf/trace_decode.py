import sys, runpy, tempfile, json, os, subprocess, re, math, wave, struct, random
from pathlib import Path
bench=Path('/Users/nschimme/gitprojects/faac-benchmark')
sys.path.insert(0,str(bench))
from codec_bench.encoders import FAACEncoder
ns=runpy.run_path('/private/tmp/faac-he-v1-probes/probe/run_features.py',run_name='trace')
clips=['sandman.16b48k.wav','velvet.16b48k.wav','21-classic.441.16b48k.wav','fms.wav']
encbin=Path('/private/tmp/faac-he-v1-invf/build-probe/frontend/faac')
fdk=Path('/Users/nschimme/gitprojects/faac-benchmark/bin/fdkdec')
faad=Path('/private/tmp/faac-he-v1-decoder-build/frontend/faad')
td=tempfile.TemporaryDirectory(prefix='faac-invf-trace-'); root=Path(td.name)
sources=[(name,bench/'data/external/audio'/name) for name in clips]
rate=48000
for name,kind in [('synthetic-high14k.wav','high14'),('synthetic-white.wav','white')]:
    src=root/name; rng=random.Random(17)
    with wave.open(str(src),'wb') as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(rate)
        data=bytearray()
        for i in range(rate*5):
            v=.7*math.sin(2*math.pi*14000*i/rate) if kind=='high14' else .25*rng.uniform(-1,1)
            x=max(-32768,min(32767,round(v*32767))); data += struct.pack('<hh',x,x)
        w.writeframes(data)
    sources.append((name,src))
rows=[]
for name,src in sources:
    enc=root/(Path(name).stem+'.m4a')
    cmd=FAACEncoder('adaptive-invf',str(encbin),profile='he').get_encode_cmd(str(src),str(enc),64,2,48000)
    env=dict(os.environ)
    for key in list(env):
        if key.startswith('FAAC_SBR_'): del env[key]
    env.update({'FAAC_SBR_ADAPTIVE_NOISE':'1','FAAC_SBR_ADAPTIVE_INVF':'1','FAAC_SBR_NOISE_TRACE':'1'})
    p=subprocess.run(cmd,env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=True)
    modes=[]; invfvals=[]; noisevals=[]
    for line in p.stderr.splitlines():
        m=re.search(r'SBR invf ch=(\d+) filteredTorig=([^ ]+) filteredTsource=([^ ]+) sourceRegion=(\d+) origRegion=(\d+) mode=(\d+)',line)
        if m:
            vals=[float(m.group(2)),float(m.group(3))]
            if not all(math.isfinite(x) for x in vals): raise RuntimeError('nonfinite INVF trace: '+line)
            invfvals.append(vals); modes.append(int(m.group(6)))
        n=re.search(r'SBR noise ch=\d+ env=\d+ Torig=([^ ]+) Tsource=([^ ]+) rawQ=([^ ]+) smoothedQ=([^ ]+) finalLevel=(\d+)',line)
        if n:
            vals=[float(n.group(i)) for i in range(1,5)]
            if not all(math.isfinite(x) for x in vals): raise RuntimeError('nonfinite noise trace: '+line)
            noisevals.append(vals)
    if not modes: raise RuntimeError('no INVF trace for '+name)
    decoders=[
        ('fdk',[str(fdk),str(enc),str(root/(Path(name).stem+'-fdk.wav'))]),
        ('faad',[str(faad),'--strict','--overwrite','-o',str(root/(Path(name).stem+'-faad.wav')),str(enc)]),
        ('ffmpeg',['ffmpeg','-v','error','-xerror','-i',str(enc),'-f','null','-'])
    ]
    results=[]
    for d,argv in decoders:
        q=subprocess.run(argv,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        if q.returncode or (d=='ffmpeg' and q.stderr): raise RuntimeError(f'{name} {d}: {q.returncode} {q.stderr[-1000:]}')
        results.append({'decoder':d,'returncode':q.returncode,'stderr':q.stderr})
    rows.append({'clip':name,'mode_histogram':{str(k):modes.count(k) for k in sorted(set(modes))},'invf_trace_count':len(modes),'noise_trace_count':len(noisevals),'filtered_T_minmax':{'original':[min(v[0] for v in invfvals),max(v[0] for v in invfvals)],'source':[min(v[1] for v in invfvals),max(v[1] for v in invfvals)]},'noise_q_minmax':{'raw':[min(v[2] for v in noisevals),max(v[2] for v in noisevals)] if noisevals else None,'smoothed':[min(v[3] for v in noisevals),max(v[3] for v in noisevals)] if noisevals else None},'decoders':results})
print(json.dumps(rows,sort_keys=True))
td.cleanup()
