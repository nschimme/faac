import os
import subprocess
import json

faac_bin = "./build/frontend/faac"
lib_path = "./build/libfaac/libfaac.so"

configs = [
    {"name": "master_like", "coll": "1.0", "is": "5500", "thr": "2.0"},
    {"name": "baked_defaults", "coll": "1.5", "is": "5500", "thr": "1.6"},
    {"name": "baked_no_coll", "coll": "1.0", "is": "5500", "thr": "1.6"},
]

for cfg in configs:
    run_name = cfg["name"]
    out = f"out_grid_{run_name}.json"
    env = os.environ.copy()
    env["FAAC_COLL_THR_MID"] = cfg["coll"]
    env["FAAC_IS_FREQ_LO"] = cfg["is"]
    env["FAAC_THRSIDE_SCALE"] = cfg["thr"]

    subprocess.run([
        "python3", "/opt/faac-benchmark/run_benchmark.py",
        faac_bin, lib_path, run_name, out,
        "--scenarios", "music_std",
        "--include-tests", "Robots_old.16b48k.wav",
        "--backend", "visqol-python"
    ], env=env)

    with open(out, "r") as f:
        data = json.load(f)
        mos = data["matrix"]["music_std_Robots_old.16b48k.wav"]["mos"]
        print(f"{run_name}: MOS={mos}")
