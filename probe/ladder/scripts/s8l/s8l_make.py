"""Make the four S8-L arms from the existing S4-B2 builder. Usage: s8l_make.py 96|128."""
import os
import pathlib
import subprocess
import sys

rate = sys.argv[1]
if rate not in ("96", "128"):
    raise SystemExit("rate must be 96 or 128")
repo = pathlib.Path(__file__).resolve().parents[4]
work = repo / "probe_tmp/s8l" / rate / "g"
env = dict(os.environ, LADDER_ARMS="K0,rSFr,rSFr0,rSFr1")
subprocess.run([sys.executable, str(repo / "probe/ladder/scripts/s4/b2_make.py"), str(work)], env=env, check=True)
