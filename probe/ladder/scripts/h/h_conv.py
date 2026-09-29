import sys
# FAAD_LADDER_DUMP C records lack the g= token core_inject needs; take group lengths from the I record.
def conv(src,dst):
    lines=open(src).read().splitlines();glen={}
    for ln in lines:
        if ln.startswith('I '):
            t=ln.split('|')[0].split();fr,ch,ng=int(t[1]),int(t[2]),int(t[6])
            gl=ln.split('|')[1].split()[:ng];glen[(fr,ch)]=','.join(gl)
    with open(dst,'w') as o:
        for ln in lines:
            if ln.startswith('C '):
                t=ln.split();o.write(ln+' g='+glen[(int(t[1]),int(t[2]))]+'\n')
if __name__=='__main__':conv(sys.argv[1],sys.argv[2])
