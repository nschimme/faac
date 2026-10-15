#!/usr/bin/env python3
"""Summarize the matched GCC13 tier17 production footprint matrix."""

import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent
TIER = ROOT / "tier17"
STAGED = Path("/private/tmp/faac-he-v1-tier17-gcc13")
VARIANTS = {
    "base": ("99ce2b0980180194f9ccd1993a786208d277cf3e", None),
    "tier17_base": ("99ce2b0980180194f9ccd1993a786208d277cf3e", TIER / "tier17-base.patch"),
    "stack": ("5d1bd74703657ae6e9acc5fae1df63ce07190071", None),
    "tier17_stack": ("5d1bd74703657ae6e9acc5fae1df63ce07190071", TIER / "tier17-stack.patch"),
}


def sha(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def tree_sha(path: Path) -> str:
    h = hashlib.sha256()
    for file in sorted(p for p in path.rglob("*") if p.is_file()):
        h.update(file.relative_to(path).as_posix().encode() + b"\0")
        h.update(file.read_bytes() + b"\0")
    return h.hexdigest()


def load_sections(name: str) -> dict:
    result = {}
    for line in (TIER / name.replace("_", "-") / "sections.txt").read_text().splitlines():
        m = re.match(r"\s*(\.(?:text|rodata|data|bss))\s+([0-9]+)\s+", line)
        if m:
            result[m.group(1)] = int(m.group(2), 10)
    return result


def load_sizes(name: str) -> dict:
    return {key: int(value) for key, value in
            (line.split("=", 1) for line in (TIER / name.replace("_", "-") / "production-sizeof.txt").read_text().splitlines())}


variants = {}
for name, (head, patch) in VARIANTS.items():
    stage_name = name.replace("_", "-")
    sections = load_sections(name)
    size_values = load_sizes(name)
    variants[name] = {
        "source_head": head,
        "source_tree_sha256": tree_sha(STAGED / stage_name),
        "source_patch_sha256": sha(patch) if patch else None,
        "library_sha256": (TIER / name.replace("_", "-") / "library.sha256").read_text().split()[0],
        "sections_bytes": sections,
        "rom_text_rodata_data_bytes": sum(sections[key] for key in (".text", ".rodata", ".data")),
        "production_sizes_excluding_resampler": size_values,
        "build_log": str(TIER / name.replace("_", "-") / "build.log"),
    }

summary = {
    "schema": 1,
    "compiler": "GCC 13.3.0, x86_64 Linux (linux/amd64 Docker image under Apple Silicon emulation)",
    "docker_image": "faac-gcc13:latest sha256:5b57ea918f81ba4868d1438f76c7a5023fad1215818d43a9282a6429e389febe",
    "configuration": "Meson release, default_library=both, b_lto=true; same /work/source and /tmp/faac-build paths, separate Docker runs",
    "metric": ".text + .rodata + .data ROM; .bss and production SBR allocations excluding resampler reported separately",
    "variants": variants,
    "tier17_delta": {
        parent: {
            "rom_bytes": variants[candidate]["rom_text_rodata_data_bytes"] - variants[parent]["rom_text_rodata_data_bytes"],
            "allocation_bytes_by_channels": {
                str(ch): variants[candidate]["production_sizes_excluding_resampler"][f"channels_{ch}_total_excluding_resampler"] - variants[parent]["production_sizes_excluding_resampler"][f"channels_{ch}_total_excluding_resampler"]
                for ch in (1, 2, 4, 8)
            },
        }
        for parent, candidate in (("base", "tier17_base"), ("stack", "tier17_stack"))
    },
    "stream_equivalence": {
        "tier17_base": "196/196 ordered AAC packet identity and strict FDK PCM hash matches tier17 hybrid base",
        "tier17_stack": "196/196 ordered AAC packet identity and strict FDK PCM hash matches tier17 hybrid stack",
    },
}
(ROOT / "tier17" / "footprint.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")

lines = [
    "# Matched GCC13 production footprint: tier17 envelope threshold",
    "",
    "GCC 13.3.0 x86_64 Linux release/LTO builds used the same `/work/source` and `/tmp/faac-build` paths in separate Docker runs. The amd64 image ran under Apple Silicon emulation; these measurements cover code size and allocation structure, not native performance.",
    "",
    "| Variant | `.text` | `.rodata` | `.data` | ROM total | `.bss` | Library SHA-256 |",
    "|---|---:|---:|---:|---:|---:|---|",
]
for name in VARIANTS:
    item = variants[name]
    sec = item["sections_bytes"]
    lines.append(f"| {name} | {sec['.text']} | {sec['.rodata']} | {sec['.data']} | {item['rom_text_rodata_data_bytes']} | {sec['.bss']} | `{item['library_sha256']}` |")
lines += ["", "| Context | ROM delta | Allocation delta at 1 / 2 / 4 / 8 channels |", "|---|---:|---|"]
for parent, label in (("base", "upstream base"), ("stack", "retained stack parent")):
    delta = summary["tier17_delta"][parent]
    alloc = delta["allocation_bytes_by_channels"]
    lines.append(f"| {label} → tier17 | {delta['rom_bytes']} B | {alloc['1']} / {alloc['2']} / {alloc['4']} / {alloc['8']} B |")
lines += [
    "",
    "Allocation totals use `sizeof(SBRInfo) + n*sizeof(SBRChannel)`, `sizeof(SBRContext) + FIFO*(sizeof(SbrFrameData) + n*sizeof(SbrEnvData))`, and `sizeof(SignalAnalysis) + n*sizeof(SignalAnalysisChannel)`. Resampler storage and allocator overhead are excluded. Exact 196/196 packet and strict FDK PCM matches are recorded for baseline and retained-stack contexts in the production-equivalence archive.",
    "",
    "The four build logs, section listings, library hashes, size-model output, source tree hashes, and tier17 source patches are adjacent to this report and summarized in `footprint.json`.",
    "",
]
(ROOT / "tier17" / "RESULTS.md").write_text("\n".join(lines))
print(ROOT / "tier17" / "RESULTS.md")
