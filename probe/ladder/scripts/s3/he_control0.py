"""E0.2 Control 0 at HE: Apple HE dump -> parse_dump -> reemit_tool at the 24 kHz core -> splice Apple's SBR tail
-> decode must equal Apple's own decode (same decoder, ADTS, sample-exact).
usage: he_control0.py <G>   (G has g2_index.json and <k>_apple.dump)"""
import sys,os,json,pathlib,subprocess,numpy as np
here=pathlib.Path(__file__).resolve();sys.path.insert(0,str(here.parent))
from he_splice import raw_aus,cpe_bits,splice,adts,adts_frames
G=pathlib.Path(sys.argv[1]);repo=here.parents[4];faad='/tmp/faad-ladder-dump/build_faad/frontend/faad'
def dec(path,out,dump=None):
    env=dict(os.environ)
    if dump:
        pathlib.Path(dump).unlink(missing_ok=True);env.update(FAAD_DUMP=str(dump),FAAD_LADDER_DUMP='1')
    subprocess.run([faad,'-q','-f','raw','-o',str(out),str(path)],env=env,capture_output=True,check=True)
    x=np.fromfile(out,'<i2');os.remove(out);return x
ok=0;res={}
for it in json.load(open(G/'g2_index.json')):
    k=it['id'];au=raw_aus(it['ref'],G/'tmp.adts')
    ap=G/f'{k}_c0_apple.adts';open(ap,'wb').write(b''.join(adts(a,24000) for a in au))
    ya=dec(ap,G/'a.raw',G/f'{k}_c0_apple_adts.dump');ab=cpe_bits(G/f'{k}_c0_apple_adts.dump')
    b=G/f'{k}_c0.bin';r=G/f'{k}_c0_core.adts'
    subprocess.run([sys.executable,str(repo/'probe/ladder/parse_dump.py'),str(G/f'{k}_c0_apple_adts.dump'),str(b)],check=True,capture_output=True)
    subprocess.run([str(repo/'probe/ladder/reemit_tool'),str(b),str(r),'24000'],check=True,capture_output=True)
    fau=adts_frames(open(r,'rb').read())
    # core-only reemit frames carry no SBR, decode them once to read their CPE lengths
    dec(r,G/'c.raw',G/f'{k}_c0_core.dump');fb=cpe_bits(G/f'{k}_c0_core.dump')
    n=min(len(fau),len(au))
    sp=G/f'{k}_c0_splice.adts';open(sp,'wb').write(b''.join(adts(splice(fau[i],fb[i+1],au[i],ab[i+1]),24000) for i in range(n)))
    ys=dec(sp,G/'s.raw');m=min(len(ya),len(ys));d=int(np.count_nonzero(ya[:m]!=ys[:m]))
    samecore=sum(fau[i][:0]==b'' and splice(fau[i],fb[i+1],au[i],ab[i+1])==au[i][:len(splice(fau[i],fb[i+1],au[i],ab[i+1]))] for i in range(n))
    res[k]={'stem':it['stem'],'rms':float(np.sqrt(np.mean(ya.astype(float)**2))),'frames':n,'apple_frames':len(au),'diff_samples':d,'n':m,'bitexact_frames':samecore}
    ok+=d==0 and n==len(au);print(k,it['stem'],res[k],flush=True)
    for f in (ap,b,r,sp,G/f'{k}_c0_core.dump',G/f'{k}_c0_apple_adts.dump'):pathlib.Path(f).unlink(missing_ok=True)
json.dump(res,open(G/'he_control0.json','w'),indent=1);print(f'HE Control 0: {ok}/{len(res)} exact')
