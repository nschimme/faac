import sys, glob, os, collections, statistics as st
OFF=[0,4,8,12,16,20,24,28,32,36,40,48,56,64,72,80,88,96,108,120,132,144,160,176,196,216,240,264,292,320,352,384,416,448,480,512,544,576,608,640,672,704,736,768,800,832,864,896,928,1024]
def hz(k): return OFF[k]*24000/1024
REG=[(0,2000),(2000,6000),(6000,12000),(12000,24000)]
def region(sfb):
    f=OFF[sfb]*24000/1024
    for i,(a,b) in enumerate(REG):
        if a<=f<b: return i
    return 3
def parse(path):
    M=collections.Counter(); 
    for line in open(path):
        t=line.split()
        if not t: continue
        if t[0]=='B':
            v=list(map(int,t[1:]))
            if len(v)>=9 and v[0]>2:
                for k,n in zip(('tot','hdr','sect','sf','spec','aux'),v[1:7]): M[k]+=n
                M['frames']+=1
        elif t[0]=='C':
            bits_part,_,rest=line.partition('|')
            h=bits_part.split(); ch=int(h[2]); win=int(h[4]); msf=int(h[5]); ng=int(h[6])
            if int(h[1])<3: continue
            M['ics']+=1
            if win==2: M['short']+=1
            else: M['long']+=1; M['msf_sum']+=msf; M['bw_hz']+=hz(msf)
            M['gg']+=int(h[7])
            if win==2: continue
            toks=rest.replace('/',' ').split()
            for sfb,tk in enumerate(toks):
                cb,sf,nnz,ms=map(int,tk.split(':'))
                r=region(sfb)
                M[f'bands{r}']+=1
                M[f'nnz{r}']+=nnz
                if cb==0: M[f'zero{r}']+=1
                elif cb==13: M[f'pns{r}']+=1
                elif cb in (14,15): M[f'is{r}']+=1
                else:
                    M[f'coded{r}']+=1; M[f'sf{r}']+=sf
                    if cb>=5: M[f'hi{r}']+=1
                    if ms: M[f'ms{r}']+=1
    return M
def main():
    rate=sys.argv[1]; d=f'd/{rate}'
    names=sorted({os.path.basename(p).rsplit('.',2)[0] for p in glob.glob(d+'/*.faac.dump')})
    names=[n for n in names if os.path.exists(f'{d}/{n}.apple.dump')]
    agg={'faac':collections.defaultdict(list),'apple':collections.defaultdict(list)}
    for n in names:
        for e in agg:
            M=parse(f'{d}/{n}.{e}.dump'); f=M['frames'] or 1; ics=M['ics'] or 1; lg=M['long'] or 1
            a=agg[e]
            a['bytes/frame'].append(M['tot']/f/8)
            for k in ('sect','sf','spec','aux','hdr'): a[k+'%'].append(100*M[k]/(M['tot'] or 1))
            a['short%'].append(100*M['short']/ics)
            a['bw_kHz(long)'].append(M['bw_hz']/lg/1000)
            for r,(lo,hi) in enumerate(REG):
                b=M[f'bands{r}'] or 1
                a[f'zero%[{lo//1000}-{hi//1000}k]'].append(100*M[f'zero{r}']/b)
                a[f'pns%[{lo//1000}-{hi//1000}k]'].append(100*M[f'pns{r}']/b)
                c=M[f'coded{r}'] or 1
                a[f'ms%coded[{lo//1000}-{hi//1000}k]'].append(100*M[f'ms{r}']/c)
                a[f'hiCB%coded[{lo//1000}-{hi//1000}k]'].append(100*M[f'hi{r}']/c)
                a[f'meanSF[{lo//1000}-{hi//1000}k]'].append(M[f'sf{r}']/c)
                a[f'nnz/long ics[{lo//1000}-{hi//1000}k]'].append(M[f'nnz{r}']/lg)
    print(f'## {rate} kbps, {len(names)} clips (mean over clips)')
    print(f'{"metric":28s} {"FAAC":>9s} {"Apple":>9s} {"A-F":>9s}')
    for k in agg['faac']:
        f=st.mean(agg['faac'][k]); a=st.mean(agg['apple'][k])
        print(f'{k:28s} {f:9.2f} {a:9.2f} {a-f:9.2f}')
main()
