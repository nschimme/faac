#!/usr/bin/env python3
"""Stage clean matched GCC13 source snapshots for tier17 footprint builds."""

from __future__ import annotations

import hashlib
import json
import subprocess
import tarfile
from io import BytesIO
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEST = Path("/private/tmp/faac-he-v1-tier17-gcc13")
SOURCES = {
    "base": (Path("/private/tmp/faac-envcount-tier17-base"), "99ce2b0980180194f9ccd1993a786208d277cf3e", False),
    "tier17-base": (Path("/private/tmp/faac-envcount-tier17-base"), "99ce2b0980180194f9ccd1993a786208d277cf3e", True),
    "stack": (Path("/private/tmp/faac-envcount-tier17-stack"), "5d1bd74703657ae6e9acc5fae1df63ce07190071", False),
    "tier17-stack": (Path("/private/tmp/faac-envcount-tier17-stack"), "5d1bd74703657ae6e9acc5fae1df63ce07190071", True),
}


def run(args: list[str], **kwargs) -> bytes:
    return subprocess.run(args, check=True, stdout=subprocess.PIPE, **kwargs).stdout


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> None:
    manifest = {"schema": 1, "variants": {}}
    for name, (repo, expected_head, apply_worktree_diff) in SOURCES.items():
        head = run(["git", "-C", str(repo), "rev-parse", "HEAD"]).decode().strip()
        if head != expected_head:
            raise RuntimeError(f"{name}: unexpected HEAD {head}")
        dest = DEST / name
        dest.mkdir(parents=True, exist_ok=True)
        archive = run(["git", "-C", str(repo), "archive", "--format=tar", "HEAD"])
        with tarfile.open(fileobj=BytesIO(archive), mode="r:") as tf:
            tf.extractall(dest, filter="data")
        patch = run(["git", "-C", str(repo), "diff", "--binary", "HEAD"]) if apply_worktree_diff else b""
        if apply_worktree_diff:
            patch_path = ROOT / f"tier17-{name}.patch"
            patch_path.write_bytes(patch)
            if not patch:
                raise RuntimeError(f"{name}: expected tier17 diff is empty")
            subprocess.run(["git", "-C", str(dest), "apply", str(patch_path)], check=True)
        manifest["variants"][name] = {
            "head": head,
            "source_patch_sha256": sha(patch) if patch else None,
            "staged_source": str(dest),
        }
    manifest_path = ROOT / "tier17-gcc13-staging.json"
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    print(manifest_path)


if __name__ == "__main__":
    main()
