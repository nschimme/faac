import subprocess
import os

faac_exe = "./build-mingw/frontend/faac.exe"
env = os.environ.copy()
env["WINEPATH"] = os.path.abspath("./build-mingw/libfaac")
env["WINEDEBUG"] = "-all"

for i in range(50):
    print(f"Iteration {i}...")
    with open("input.wav", "rb") as fin:
        res = subprocess.run(["wine", faac_exe, "-o", "-", "-"], stdin=fin, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, env=env)
        if res.returncode != 0:
            print(f"CRASHED at iteration {i} with code {res.returncode}")
            break
