#!/usr/bin/env python3
"""Matched clip-ladder comparisons using the established benchmark helpers."""
import argparse,json,sys,statistics
from pathlib import Path
sys.path.insert(0,"/Users/nschimme/gitprojects/faac-benchmark")
sys.path.insert(0,"/Users/nschimme/gitprojects/faac-benchmark/scripts")
from compare_results import bootstrap_mean_ci
from bd_rate import bd_rate_curve
p=argparse.ArgumentParser();p.add_argument("rows",type=Path);p.add_argument("--pairs",required=True);p.add_argument("--output",type=Path,required=True);a=p.parse_args()
rows=list(json.loads(a.rows.read_text())["rows"].values());idx={(r["variant"],r["clip"],r["target_kbps"]):r for r in rows}
report={"source":str(a.rows),"metric":"Zimtohrli audio-mode MOS vs AAC payload bitrate; PCHIP BD-rate", "bootstrap":"clip mean, n=2000 seed=0", "comparisons":{}}
for pair in a.pairs.split(","):
 cand,ref=pair.split(":");clips=sorted({r["clip"] for r in rows if r["variant"]==cand});rates=sorted({r["target_kbps"] for r in rows if r["variant"]==cand});per=[];skipped=[];packet=pcm=0;mos=[]
 for clip in clips:
  b=[];c=[]
  for rate in rates:
   rr=idx[ref,clip,rate];cr=idx[cand,clip,rate]
   if rr["profile"]!="HE-AAC" or cr["profile"]!="HE-AAC":raise RuntimeError("Wrong profile")
   b.append((rr["actual_payload_kbps"],rr["mos"]));c.append((cr["actual_payload_kbps"],cr["mos"]))
   packet+=rr["payload_sha256"]==cr["payload_sha256"];pcm+=rr["decoded_sha256"]==cr["decoded_sha256"]
   mos.append({"clip":clip,"rate":rate,"delta":cr["mos"]-rr["mos"]})
  value=bd_rate_curve(b,c)
  if value is None:skipped.append(clip)
  else:per.append({"clip":clip,"bd_rate_percent":value})
 vals=[r["bd_rate_percent"] for r in per];pr={}
 for rate in rates:
  ds=[idx[cand,clip,rate]["mos"]-idx[ref,clip,rate]["mos"] for clip in clips]
  dr=[100*(idx[cand,clip,rate]["actual_payload_kbps"]/idx[ref,clip,rate]["actual_payload_kbps"]-1) for clip in clips]
  pr[str(rate)]={"mean_mos_delta":statistics.mean(ds),"mos_ci":bootstrap_mean_ci(ds),"mean_actual_rate_delta_percent":statistics.mean(dr)}
 report["comparisons"][pair]={"mean_bd_rate_percent":statistics.mean(vals),"median_bd_rate_percent":statistics.median(vals),"ci":bootstrap_mean_ci(vals),"valid_clips":len(vals),"skipped_clips":skipped,"per_clip":per,"per_rate":pr,"packet_ties":packet,"pcm_ties":pcm,"rows":len(clips)*len(rates),"worst_clip":max(per,key=lambda r:r["bd_rate_percent"]),"worst_mos_row":min(mos,key=lambda r:r["delta"])}
a.output.write_text(json.dumps(report,indent=2)+"\n")
for name,r in report["comparisons"].items():print(name,r["mean_bd_rate_percent"],r["ci"],r["worst_clip"])
