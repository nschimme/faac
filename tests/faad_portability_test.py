#!/usr/bin/env python3
"""Run FAAD tests under Linux QEMU on 32/64-bit, little/big-endian CPUs.

Requires meson, ninja, qemu-user and GCC cross toolchains with libc development
sysroots for i686, x86_64, powerpc and s390x. Source can be mounted read-only;
builds are temporary.
Usage: python3 tests/faad_portability_test.py
"""
import pathlib
import shutil
import subprocess
import tempfile


TARGETS = (
    ("i686", "x86", "i686", "little", "qemu-i386"),
    ("x86_64", "x86_64", "x86_64", "little", "qemu-x86_64"),
    ("powerpc", "ppc", "powerpc", "big", "qemu-ppc"),
    ("s390x", "s390x", "s390x", "big", "qemu-s390x"),
)


def main():
    source = pathlib.Path(__file__).resolve().parents[1]
    for triple, family, cpu, endian, qemu in TARGETS:
        compiler = f"{triple}-linux-gnu-gcc"
        for tool in (compiler, qemu, "meson", "ninja"):
            if not shutil.which(tool):
                raise SystemExit(f"Missing portability tool: {tool}")
        if not pathlib.Path(f"/usr/{triple}-linux-gnu/include/stdio.h").is_file():
            raise SystemExit(f"Missing libc development sysroot: {triple}")
        with tempfile.TemporaryDirectory(prefix=f"faad-{triple}-") as tmp:
            cross = pathlib.Path(tmp) / "cross.ini"
            cross.write_text(
                f"[binaries]\nc = '{compiler}'\nar = '{triple}-linux-gnu-ar'\n"
                f"strip = '{triple}-linux-gnu-strip'\n"
                f"exe_wrapper = ['{qemu}', '-L', '/usr/{triple}-linux-gnu']\n"
                f"[host_machine]\nsystem = 'linux'\ncpu_family = '{family}'\n"
                f"cpu = '{cpu}'\nendian = '{endian}'\n"
                "[properties]\nneeds_exe_wrapper = true\n"
                "[built-in options]\nc_args = ['-DFAAD_TEST_SECONDS=1']\n"
            )
            build = pathlib.Path(tmp) / "build"
            subprocess.run([
                "meson", "setup", str(build), str(source), "--cross-file", str(cross),
                "--buildtype=debugoptimized", "-Db_lto=false", "-Ddefault_library=static",
                "-Dfrontend=false",
            ], check=True)
            subprocess.run(["meson", "test", "-C", str(build), "--print-errorlogs",
                            "--timeout-multiplier", "10"], check=True)
            print(f"PASS: {triple} ({endian}-endian)", flush=True)


if __name__ == "__main__":
    main()
