"""HE core splicing. Raw AUs from an m4a (via ffmpeg ADTS copy); a core-only AU stream from FAAC's writer or a
FAAC HE encode; splice = core CPE bits of stream X + Apple's FIL/SBR tail (up to and incl. ID_END), re-padded,
written as ADTS (LC, core rate; the decoder finds SBR implicitly).
Element bit lengths come from the FAAD_LADDER_DUMP C records (bits field of ch 0 = whole CPE after the 3-bit id)."""
import subprocess,os
SR_IDX={96000:0,88200:1,64000:2,48000:3,44100:4,32000:5,24000:6,22050:7,16000:8}
def adts_frames(data):
    i=0;out=[]
    while i+7<=len(data):
        assert data[i]==0xFF and data[i+1]&0xF0==0xF0,('sync',i)
        prot_abs=data[i+1]&1;L=((data[i+3]&3)<<11)|(data[i+4]<<3)|(data[i+5]>>5);h=7 if prot_abs else 9
        out.append(data[i+h:i+L]);i+=L
    return out
def raw_aus(m4a,tmp):
    subprocess.run(['ffmpeg','-v','error','-y','-i',str(m4a),'-c:a','copy','-f','adts',str(tmp)],check=True)
    d=open(tmp,'rb').read();os.remove(tmp);return adts_frames(d)
def cpe_bits(dump):
    """frame (1-based decoder count) -> bits of the CPE element after its id."""
    r={}
    for l in open(dump):
        if l.startswith('C '):
            t=l.split();fr,ch,b=int(t[1]),int(t[2]),int(t[3])
            if ch==0:r[fr]=b
    return r
class Bits:
    def __init__(s,b):s.b=b;s.n=len(b)*8
    def get(s,pos,n):
        v=0
        for k in range(n):v=(v<<1)|((s.b[(pos+k)>>3]>>(7-((pos+k)&7)))&1)
        return v
def tail_bits(au,start):
    """bits [start, end-of-ID_END) of an AU whose element at `start` is FIL... END."""
    B=Bits(au);p=start
    while True:
        eid=B.get(p,3);p+=3
        if eid==7:return p
        assert eid==6,('unexpected element',eid)
        cnt=B.get(p,4);p+=4
        if cnt==15:cnt+=B.get(p,8)-1;p+=8
        p+=8*cnt
def pack(bitlist):
    bitlist=bitlist+[0]*(-len(bitlist)%8);return bytes(int(''.join(map(str,bitlist[i:i+8])),2) for i in range(0,len(bitlist),8))
def tobits(au,a,b):
    B=Bits(au);return [B.get(k,1) for k in range(a,b)]
def adts(au,sr,ch=2):
    L=len(au)+7;h=[0xFF,0xF1,(1<<6)|(SR_IDX[sr]<<2)|(ch>>2),((ch&3)<<6)|(L>>11),(L>>3)&0xFF,((L&7)<<5)|0x1F,0xFC]
    return bytes(h)+au
def splice(core_au,core_bits,apple_au,apple_bits):
    """core CPE (id + core_bits) from core_au, then Apple's tail after its CPE."""
    end=tail_bits(apple_au,3+apple_bits)
    return pack(tobits(core_au,0,3+core_bits)+tobits(apple_au,3+apple_bits,end))
