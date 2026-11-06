#!/usr/bin/env python3
# Static archives share one link namespace, so no global may be defined in two of them.
import subprocess
import sys

DEFINED = set("ABCDGRSTVW")
MACHO = sys.platform == "darwin"

# The compiler, not the sources, emits these into every object that needs
# them, and each archive legitimately carries its own copy.
GENERATED = (".refptr.", "x86.get_pc_thunk.", "odr_asan.")


def globals_of(nm, archive):
    out = subprocess.run([nm, "-g", "--defined-only", archive], check=True,
                         capture_output=True, text=True).stdout
    names = set()
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1] in DEFINED:
            # Mach-O decorates C names with a leading underscore; other formats do not.
            name = parts[2][1:] if MACHO and parts[2].startswith("_") else parts[2]
            if not name.lstrip("_").startswith(GENERATED):
                names.add(name)
    return names


def main():
    nm, archives = sys.argv[1], sys.argv[2:]
    seen, bad = {}, []
    for archive in archives:
        names = globals_of(nm, archive)
        if not names:
            bad.append(f"{archive}: no globals found, nm output unparsed")
        for n in sorted(names):
            if n in seen:
                bad.append(f"{n} defined in both {seen[n]} and {archive}")
            seen[n] = archive
    if bad:
        print("\n".join(bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
