"""faam output regression: run one command matrix through a baseline and a candidate build
and byte-compare every output file, stdout and exit code, after masking each build's git hash.
Not part of `meson test`: it needs a baseline build, like compare_master_mp4.py.

usage: python3 tests/compare_faam_output.py BASELINE_BUILD_DIR CANDIDATE_BUILD_DIR
(meson build directories configured with -Dmuxer=true; the candidate's faac makes the inputs)
Set KEEP=1 to print the scratch directory holding both runs' files."""
import subprocess, sys, os, shutil, struct, math, re, tempfile

if len(sys.argv) != 3:
    sys.exit(__doc__)
old, new = (os.path.abspath(p) for p in sys.argv[1:3])
def ver(b):
    return re.search(rb"\(([0-9a-f]{8})", subprocess.run([b + "/frontend/faam", "-h"], capture_output=True).stdout).group(1)
vo, vn = ver(old), ver(new)
tmp = tempfile.mkdtemp(prefix="bytecmp")

def wav(path, rate, ch, secs):
    n = int(rate * secs)
    fr = b"".join(struct.pack("<" + "h" * ch, *[int(8000 * math.sin(2 * math.pi * (440 + 220 * c) * i / rate)) for c in range(ch)]) for i in range(n))
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 36 + len(fr)) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 1, ch, rate, rate * ch * 2, ch * 2, 16) + b"data" + struct.pack("<I", len(fr)) + fr)

src = os.path.join(tmp, "src"); os.makedirs(src)
faac = new + "/frontend/faac"
for name, rate, ch, args in [("lc", 44100, 2, []), ("mono", 48000, 1, []), ("he", 44100, 2, ["--object-type", "he-aac-v1", "-b", "48"]),
                             ("lc32", 32000, 2, ["-b", "64"])]:
    wav(f"{src}/{name}.wav", rate, ch, 3)
    subprocess.run([faac, "--creation-time", "0"] + args + ["-o", f"{src}/{name}.m4a", f"{src}/{name}.wav"], check=True, capture_output=True)
    subprocess.run([new + "/frontend/faam", "demux", f"{src}/{name}.m4a", "-o", f"{src}/{name}.aac"], check=True, capture_output=True)

CT = ["--creation-time", "0"]
cmds = []
for n in ("lc", "mono", "he", "lc32"):
    sb = ["--sbr-signaling", "compatible"] if n == "he" else []
    cmds += [(f"mux_{n}", ["-i", f"{n}.aac", "-o", f"{n}_mux.m4a"] + CT + sb),
             (f"gapless_{n}", ["-i", f"{n}.aac", "-o", f"{n}_gl.m4a", "--encoder-delay", "1024", "--padding-delay", "300"] + CT + sb),
             (f"m4b_{n}", ["-i", f"{n}.aac", "-o", f"{n}.m4b", "--brand", "m4b", "--title", "T", "--artist", "A"] + CT + sb),
             (f"tags_{n}", ["-i", f"{n}.aac", "-o", f"{n}_tg.m4a", "--title", "Tést", "--genre", "Rock", "--track", "3/12", "--tag", "K=V", "--language", "deu"] + CT + sb),
             (f"info_{n}", ["info", f"{n}.m4a"]), (f"dump_{n}", ["dump", f"{n}.m4a"]), (f"demux_{n}", ["demux", f"{n}.m4a", "-o", f"{n}_dm.aac"])]
cmds += [("multi", ["-i", "lc.aac", "-i", "mono.aac", "-o", "multi.m4a"] + CT),
         ("multi_gl", ["-i", "lc.aac", "-i", "mono.aac", "-o", "multi_gl.m4a", "--encoder-delay", "1024", "--padding-delay", "100"] + CT)]
# in-place edits on faac files and on faam files
for n in ("lc", "he"):
    cmds += [(f"tag_faac_{n}", ["tag", f"{n}.m4a", "--title", "New", "--album", "Al", "--tag", "X=Y"]),
             (f"tag_grow_{n}", ["tag", f"{n}.m4a", "--comment", "C" * 5000]),
             (f"tag_shrink_{n}", ["tag", f"{n}.m4a", "--comment", "c"]),
             (f"tag_clear_{n}", ["tag", f"{n}.m4a", "--clear"]),
             (f"tag_faam_{n}", ["tag", f"{n}_tg.m4a", "--title", "Other", "--remove", "genre"]),
             (f"info_after_{n}", ["info", f"{n}.m4a"])]
cmds += [("chap_mux", ["-i", "lc.aac", "-o", "ch.m4b", "--brand", "m4b"] + CT)]
with open(f"{src}/chapters.txt", "w") as f:
    f.write("00:00:00.000 One\n00:00:01.000 Two\n")
cmds += [("chap_import", ["chapter", "import", "ch.m4b", f"chapters.txt"]), ("chap_export", ["chapter", "export", "ch.m4b"]),
         ("bad_input", ["-i", "chapters.txt", "-o", "bad.m4a"]), ("bad_missing", ["-i", "nope.aac", "-o", "bad2.m4a"])]

def run(b, ver_):
    w = os.path.join(tmp, os.path.basename(os.path.dirname(b)) + "_w"); shutil.rmtree(w, ignore_errors=True); shutil.copytree(src, w)
    out = {}
    for name, a in cmds:
        r = subprocess.run([b + "/frontend/faam"] + a, cwd=w, capture_output=True, env={"PATH": os.environ["PATH"]})
        out[name] = (r.returncode, r.stdout.replace(ver_, b"HHHHHHHH"), r.stderr.replace(ver_, b"HHHHHHHH"))
    files = {}
    for f in sorted(os.listdir(w)):
        files[f] = open(os.path.join(w, f), "rb").read().replace(ver_, b"HHHHHHHH")
    return out, files
(o1, f1), (o2, f2) = run(old, vo), run(new, vn)
bad = 0
if os.environ.get('KEEP'): print(tmp)
for k in o1:
    if o1[k] != o2[k]:
        bad += 1; print("DIFF cmd", k, o1[k][0], o2[k][0])
for k in sorted(set(f1) | set(f2)):
    if f1.get(k) != f2.get(k):
        bad += 1; print("DIFF file", k)
ok = sum(1 for n in f1 if f1[n] == f2.get(n))
print(f"{len(o1)} commands, {len(f1)} files, {ok} identical, {bad} differences")
sys.exit(bad != 0)
