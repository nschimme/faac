#!/usr/bin/env python3
"""The static libfaad archive must export only faad_ symbols, and libfaac.a and libfaad.a
must not define a symbol in common: linking both into one program may not keep one copy.

usage: static_archives_test.py libfaac.a libfaad.a
Needs nm; skipped (exit 77) where it is missing."""
import shutil, subprocess, sys

if len(sys.argv) != 3:
    sys.exit(__doc__)
nm = shutil.which("nm")
if not nm:
    sys.exit(77)

def defined(archive):
    out = subprocess.run([nm, "-g", "--defined-only", archive], capture_output=True, text=True,
                         check=True).stdout
    syms = set()
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1] in "TDBRSCVW":
            name = parts[2].lstrip("_") if sys.platform == "darwin" else parts[2]
            # Toolchain and sanitizer runtime symbols (_GLOBAL__sub_I_*, asan.module_ctor, ...)
            if not name.startswith("_") and "." not in name:
                syms.add(name)
    return syms

faac, faad = defined(sys.argv[1]), defined(sys.argv[2])
bad = sorted(s for s in faad if not s.startswith("faad_"))
if bad:
    print("libfaad exports unprefixed symbols:", " ".join(bad))
shared = sorted(faac & faad)
if shared:
    print("defined by both libfaac and libfaad:", " ".join(shared))
sys.exit(1 if bad or shared else 0)
