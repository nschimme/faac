#!/usr/bin/env python3
"""Archive the reviewed clean tier17 stack source for GCC13 footprint build."""

import hashlib
import json
import subprocess
import tarfile
from io import BytesIO
from pathlib import Path

REPO = Path("/private/tmp/faac-he-v1-envelope-production")
DEST = Path("/private/tmp/faac-he-v1-clean-tier17-gcc13/source-final")
ART = Path(__file__).resolve().parents[1] / "production-footprint/gcc13/tier17"
HEAD_EXPECTED = "5d1bd74703657ae6e9acc5fae1df63ce07190071"


def cmd(args: list[str], *, output: bytes = None) -> bytes:
    return subprocess.run(args, check=True, stdout=subprocess.PIPE, input=output).stdout


def main() -> None:
    head = cmd(["git", "-C", str(REPO), "rev-parse", "HEAD"]).decode().strip()
    if head != HEAD_EXPECTED:
        raise RuntimeError(f"Unexpected production parent {head}")
    diff = cmd(["git", "-C", str(REPO), "diff", "--binary", "HEAD"])
    if not diff:
        raise RuntimeError("Clean tier17 source patch is empty")
    ART.mkdir(parents=True, exist_ok=True)
    patch_path = ART / "clean-tier17-stack.patch"
    patch_path.write_bytes(diff)
    DEST.mkdir(parents=True, exist_ok=True)
    archive = cmd(["git", "-C", str(REPO), "archive", "--format=tar", "HEAD"])
    with tarfile.open(fileobj=BytesIO(archive), mode="r:") as tf:
        tf.extractall(DEST, filter="data")
    subprocess.run(["git", "-C", str(DEST), "apply", str(patch_path)], check=True)
    h = hashlib.sha256()
    for file in sorted(p for p in DEST.rglob("*") if p.is_file()):
        h.update(file.relative_to(DEST).as_posix().encode() + b"\0")
        h.update(file.read_bytes() + b"\0")
    manifest = {
        "parent_head": head,
        "source_patch_sha256": hashlib.sha256(diff).hexdigest(),
        "source_tree_sha256": h.hexdigest(),
        "staged_source": str(DEST),
        "patch": str(patch_path),
    }
    out = ART / "clean-tier17-staging.json"
    out.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    print(out)


if __name__ == "__main__":
    main()
