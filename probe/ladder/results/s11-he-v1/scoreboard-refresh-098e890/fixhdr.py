import hashlib,struct,json,pathlib,sys
src=pathlib.Path('fb/data/external/audio'); dst=pathlib.Path('audio_verified'); dst.mkdir(exist_ok=True)
m=json.load(open('/home/user/faac/probe/ladder/ref/apple-s11-367cf8dc/manifest.json'))
exp={pathlib.Path(r['source_wav_path_at_capture']).name:r['source_wav_sha256'] for r in m['rows']}
ok=0
for n,h in sorted(exp.items()):
    b=(src/n).read_bytes(); i=b.find(b'LIST'); j=b.find(b'data')
    isft=b'Lavf62.12.100\0'; isft+=b'\0'*(len(isft)%2)
    info=b'INFO'+b'ISFT'+struct.pack('<I',14)+isft
    out=b[:i]+b'LIST'+struct.pack('<I',len(info))+info+b[j:]
    out=out[:4]+struct.pack('<I',len(out)-8)+out[8:]
    if hashlib.sha256(out).hexdigest()==h: ok+=1; (dst/n).write_bytes(out)
    else: print('FAIL',n)
print(ok,'/',len(exp))
