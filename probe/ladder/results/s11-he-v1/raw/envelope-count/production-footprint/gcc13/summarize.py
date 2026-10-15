#!/usr/bin/env python3
"""Summarize matched GCC 13 envelope-count production footprint artifacts."""

import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent
TMP = Path("/private/tmp/faac-he-v1-gcc13-envcount")
SOURCES = {
    "base": (TMP / "base", "99ce2b0980180194f9ccd1993a786208d277cf3e", None),
    "stack": (TMP / "stack", "5d1bd74703657ae6e9acc5fae1df63ce07190071", None),
    "all4_base": (TMP / "all4-base", "99ce2b0980180194f9ccd1993a786208d277cf3e", ROOT / "all4-base.patch"),
    "all4_stack": (TMP / "all4-stack", "5d1bd74703657ae6e9acc5fae1df63ce07190071", ROOT / "all4-stack.patch"),
}
ART_DIRS = {"all4_base": "all4-base", "all4_stack": "all4-stack"}


def sha(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def tree_sha(path: Path) -> str:
    h = hashlib.sha256()
    files = sorted(p for p in path.rglob("*") if p.is_file())
    for file in files:
        name = file.relative_to(path).as_posix()
        h.update(name.encode() + b"\0")
        h.update(file.read_bytes() + b"\0")
    return h.hexdigest()


def sections(name: str) -> dict:
    values = {}
    directory = ROOT / ART_DIRS.get(name, name)
    for line in (directory / "sections.txt").read_text().splitlines():
        m = re.match(r"\s*\d+\s+(\S+)\s+([0-9a-fA-F]+)\s+", line)
        if m and m.group(1) in {".text", ".rodata", ".data", ".bss"}:
            values[m.group(1)] = int(m.group(2), 16)
    return values


def sizes(name: str) -> dict:
    directory = ROOT / ART_DIRS.get(name, name)
    return {k: int(v) for k, v in (line.split("=", 1) for line in (directory / "production-sizeof.txt").read_text().splitlines())}


variants = {}
for name, (source, head, patch) in SOURCES.items():
    sec = sections(name)
    model = sizes(name)
    variant_dir = ROOT / ART_DIRS.get(name, name)
    variants[name] = {
        "source_head": head,
        "source_tree_sha256": tree_sha(source),
        "source_patch_sha256": sha(patch) if patch else None,
        "library_sha256": (variant_dir / "library.sha256").read_text().split()[0],
        "sections_bytes": sec,
        "rom_text_rodata_data_bytes": sum(sec.get(k, 0) for k in (".text", ".rodata", ".data")),
        "production_sizes": model,
        "build_log": str(variant_dir / "build.log"),
    }

summary = {
    "compiler": "GCC 13.3.0, x86_64 Linux (linux/amd64 Docker image under Apple Silicon emulation)",
    "docker_image": "faac-gcc13:latest sha256:5b57ea918f81ba4868d1438f76c7a5023fad1215818d43a9282a6429e389febe",
    "configuration": "Meson release, default_library=both, b_lto=true; identical /work/source and /tmp/faac-build paths for all variants",
    "rom_metric": ".text + .rodata + .data; .bss reported separately",
    "binary_equivalence": {
        "all4_base": "196/196 ordered AAC packet identity and strict FDK decoded PCM hash matches all_base",
        "all4_stack": "196/196 ordered AAC packet identity and strict FDK decoded PCM hash matches all_stack",
    },
    "variants": variants,
    "all4_incremental": {
        "base_alloc_bytes_excluding_resampler": {
            str(ch): variants["all4_base"]["production_sizes"][f"channels_{ch}_total_excluding_resampler"] - variants["base"]["production_sizes"][f"channels_{ch}_total_excluding_resampler"]
            for ch in (1, 2, 4, 8)
        },
        "stack_alloc_bytes_excluding_resampler": {
            str(ch): variants["all4_stack"]["production_sizes"][f"channels_{ch}_total_excluding_resampler"] - variants["stack"]["production_sizes"][f"channels_{ch}_total_excluding_resampler"]
            for ch in (1, 2, 4, 8)
        },
    },
}
(ROOT / "footprint.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")

lines = [
    "# Matched GCC 13 production footprint: fixed four-envelope support",
    "",
    "GCC 13.3.0 x86_64 Linux release/LTO builds were made in separate Docker runs with the same `/work/source` and `/tmp/faac-build` paths. The amd64 image ran under Apple Silicon emulation; these are code-size and `sizeof` measurements, not native performance results.",
    "",
    "| Variant | `.text` | `.rodata` | `.data` | ROM total | `.bss` | Library SHA-256 |",
    "|---|---:|---:|---:|---:|---:|---|",
]
for name in ("base", "all4_base", "stack", "all4_stack"):
    item = variants[name]
    sec = item["sections_bytes"]
    lines.append(f"| {name} | {sec['.text']} | {sec['.rodata']} | {sec['.data']} | {item['rom_text_rodata_data_bytes']} | {sec['.bss']} | `{item['library_sha256']}` |")
lines += [
    "",
    "| Four-envelope ROM delta | Bytes |",
    "|---|---:|",
    f"| Baseline all4 minus upstream baseline | {variants['all4_base']['rom_text_rodata_data_bytes'] - variants['base']['rom_text_rodata_data_bytes']} |",
    f"| Stack all4 minus retained stack parent | {variants['all4_stack']['rom_text_rodata_data_bytes'] - variants['stack']['rom_text_rodata_data_bytes']} |",
    "",
    "The production allocation model sums `sizeof(SBRInfo) + n * sizeof(SBRChannel)`, `sizeof(SBRContext) + SBR_FRAME_FIFO * (sizeof(SbrFrameData) + n * sizeof(SbrEnvData))`, and `sizeof(SignalAnalysis) + n * sizeof(SignalAnalysisChannel)`. It excludes resampler allocations and allocator overhead.",
    "",
    "| Channels | Baseline total | Base all4 total | Base delta | Stack parent total | Stack all4 total | Stack delta |",
    "|---:|---:|---:|---:|---:|---:|---:|",
]
for ch in (1, 2, 4, 8):
    base = variants["base"]["production_sizes"][f"channels_{ch}_total_excluding_resampler"]
    all4b = variants["all4_base"]["production_sizes"][f"channels_{ch}_total_excluding_resampler"]
    stack = variants["stack"]["production_sizes"][f"channels_{ch}_total_excluding_resampler"]
    all4s = variants["all4_stack"]["production_sizes"][f"channels_{ch}_total_excluding_resampler"]
    lines.append(f"| {ch} | {base} | {all4b} | +{all4b-base} | {stack} | {all4s} | +{all4s-stack} |")
lines += [
    "",
    "The GCC13 section output, build logs, library hashes, production-size model output, source-tree hashes, and patch hashes are recorded in `footprint.json` and the adjacent variant folders. All4 packet and strict FDK PCM equivalence counts are 196/196 for both base and stack contexts.",
    "",
]
(ROOT / "RESULTS.md").write_text("\n".join(lines))
print(ROOT / "RESULTS.md")
