"""Write <G>/g2_index.json for s3/he_control0.py from a reference set. usage: c0_index.py <G> <fdk-enc-dir | ref dir>"""
import sys,json,pathlib
G=pathlib.Path(sys.argv[1]);G.mkdir(parents=True,exist_ok=True);R=pathlib.Path(sys.argv[2])
fs=sorted(R.glob('X__*.m4a')) or sorted(R.glob('*.m4a'))
json.dump([{'id':f'{i:02d}','stem':f.stem.removeprefix('X__'),'ref':str(f)} for i,f in enumerate(fs)],open(G/'g2_index.json','w'),indent=0)
print(len(fs))
