import os,sys,subprocess,glob,json,concurrent.futures as cf
P='/home/user/wt/smooth-bs/build/frontend/faac';A='/opt/faac-benchmark/data/external/audio'
clips=sorted(glob.glob(A+'/*.wav'));W='/home/user/wsC/survey'
ARMS={'ctl':{},'r3':{'FAAC_BS_RATIO':'3'},'r4':{'FAAC_BS_RATIO':'4'},'r6':{'FAAC_BS_RATIO':'6'},'nodrop':{'FAAC_BS_DROPRATIO':'0'},
 'nohyst':{'FAAC_BS_NOHYST':'1'},'span0':{'FAAC_BS_PREVS':'0','FAAC_BS_NEXTS':'0'},'sm1':{'FAAC_BS_SMOOTH':'1'},'r4nd':{'FAAC_BS_RATIO':'4','FAAC_BS_DROPRATIO':'0'},'dr4':{'FAAC_BS_DROPRATIO':'4'},'dr8':{'FAAC_BS_DROPRATIO':'8'},'dr16':{'FAAC_BS_DROPRATIO':'16'},'dr32':{'FAAC_BS_DROPRATIO':'32'},'dr12':{'FAAC_BS_DROPRATIO':'12'}}
arms=sys.argv[1:] or list(ARMS)
def run(a,c):
    st=os.path.basename(c)[:-4];d=f'{W}/{a}_{st}.bs';o=f'{W}/{a}.m4a.{os.getpid()}'
    env=dict(os.environ,FAAC_BS_DUMP=d,**ARMS[a]);subprocess.run([P,'-b','128','-o','/dev/null' if False else f'{W}/{a}_{st}.m4a','--overwrite',c],env=env,capture_output=True)
    n=s=0
    for l in open(d):
        f=l.split()
        if f[2]=='0':n+=1;s+=f[5] in('1','2')   # LONG_SHORT or ONLY_SHORT counted as short-ish; 2=ONLY_SHORT
    os.remove(f'{W}/{a}_{st}.m4a');return a,st,s/n
res={}
with cf.ThreadPoolExecutor(3) as ex:
    for a,st,fr in ex.map(lambda ac:run(*ac),[(a,c) for a in arms for c in clips]):res.setdefault(a,{})[st]=fr
json.dump(res,open(W+'/short.json','w'),indent=1)
key=['girl.16b48k','Last_Of_The_Mohicanz__Sample_.16b48k','velvet.16b48k','bas','Changes.16b48k','Shinsho_pool_3min45_4min4.16b48k','take_your_finger_frin_my_head.16b48k','fms','24-Greensleeves-Korean-male-speech.441.16b48k']
print('arm   mean  '+' '.join(k[:7] for k in key))
for a in arms:print(f"{a:6s} {sum(res[a].values())/len(res[a]):.3f} "+' '.join(f"{res[a][k]:.2f}".rjust(7) for k in key))
