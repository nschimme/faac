#!/usr/bin/env python3
"""Summarize the final clean tier17 stack candidate against the matched parent."""

import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent
TIER = ROOT / "tier17"
STAGED = Path("/private/tmp/faac-he-v1-clean-tier17-gcc13/source-final")
MANIFEST = json.loads((TIER / "clean-tier17-staging.json").read_text())


def tree_sha(path: Path) -> str:
    h = hashlib.sha256()
    for file in sorted(p for p in path.rglob("*") if p.is_file()):
        h.update(file.relative_to(path).as_posix().encode() + b"\0")
        h.update(file.read_bytes() + b"\0")
    return h.hexdigest()


def section_values(path: Path) -> dict:
    result = {}
    for line in path.read_text().splitlines():
        m = re.match(r"\s*(\.(?:text|rodata|data|bss))\s+([0-9]+)\s+", line)
        if m:
            result[m.group(1)] = int(m.group(2))
    return result


def size_values(path: Path) -> dict:
    return {k: int(v) for k, v in (line.split("=", 1) for line in path.read_text().splitlines())}


parent_dir = TIER / "stack"
candidate_dir = TIER / "clean-tier17-stack"
parent_sections = section_values(parent_dir / "sections.txt")
candidate_sections = section_values(candidate_dir / "sections.txt")
parent_sizes = size_values(parent_dir / "production-sizeof.txt")
candidate_sizes = size_values(candidate_dir / "production-sizeof.txt")
parent_rom = sum(parent_sections[k] for k in (".text", ".rodata", ".data"))
candidate_rom = sum(candidate_sections[k] for k in (".text", ".rodata", ".data"))
summary = {
    "compiler": "GCC 13.3.0 x86_64 Linux, faac-gcc13 image under Apple Silicon emulation",
    "docker_image_sha256": "5b57ea918f81ba4868d1438f76c7a5023fad1215818d43a9282a6429e389febe",
    "configuration": "Meson release, default_library=both, b_lto=true; same /work/source and /tmp/faac-build paths",
    "source_parent_head": MANIFEST["parent_head"],
    "source_patch_sha256": MANIFEST["source_patch_sha256"],
    "source_tree_sha256": tree_sha(STAGED),
    "binary_sha256": (candidate_dir / "library.sha256").read_text().split()[0],
    "variants": {
        "stack_parent": {"sections_bytes": parent_sections, "rom_text_rodata_data_bytes": parent_rom,
                         "production_sizes_excluding_resampler": parent_sizes},
        "clean_tier17_stack": {"sections_bytes": candidate_sections, "rom_text_rodata_data_bytes": candidate_rom,
                               "production_sizes_excluding_resampler": candidate_sizes},
    },
    "delta": {
        "rom_bytes": candidate_rom - parent_rom,
        "bss_bytes": candidate_sections[".bss"] - parent_sections[".bss"],
        "persistent_sbr_allocation_bytes_by_channels": {
            str(ch): candidate_sizes[f"channels_{ch}_total_excluding_resampler"] - parent_sizes[f"channels_{ch}_total_excluding_resampler"]
            for ch in (1, 2, 4, 8)
        },
        "SbrWrite_stack_scratch_bytes": 2048,
        "SbrWrite_stack_scratch_change_bytes": 1024,
    },
    "equivalence": "196/196 AAC packet + strict FDK PCM matches to measured all_stack; additional LC 16/16 exact and HE boundary 16/16 FDK/FAAD/FFmpeg pass",
    "timing": "Not measured; this is emulated amd64 code-size evidence, not native timing.",
}
out_json = TIER / "clean-tier17-footprint.json"
out_json.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")

lines = [
    "# Final clean tier17 stack footprint",
    "",
    "Matched GCC 13.3.0 x86_64 Linux release/LTO builds ran under Apple Silicon emulation with identical `/work/source` and `/tmp/faac-build` paths. This is code-size and allocation evidence, not native timing.",
    "",
    "| Variant | `.text` | `.rodata` | `.data` | ROM total | `.bss` |",
    "|---|---:|---:|---:|---:|---:|",
    f"| Stack parent | {parent_sections['.text']} | {parent_sections['.rodata']} | {parent_sections['.data']} | {parent_rom} | {parent_sections['.bss']} |",
    f"| Clean tier17 stack | {candidate_sections['.text']} | {candidate_sections['.rodata']} | {candidate_sections['.data']} | {candidate_rom} | {candidate_sections['.bss']} |",
    f"| Delta | {candidate_sections['.text']-parent_sections['.text']:+} | {candidate_sections['.rodata']-parent_sections['.rodata']:+} | {candidate_sections['.data']-parent_sections['.data']:+} | {candidate_rom-parent_rom:+} | {candidate_sections['.bss']-parent_sections['.bss']:+} |",
    "",
    "| Channels | Persistent SBR allocation delta (resampler excluded) |",
    "|---:|---:|",
]
for ch in (1, 2, 4, 8):
    delta = candidate_sizes[f"channels_{ch}_total_excluding_resampler"] - parent_sizes[f"channels_{ch}_total_excluding_resampler"]
    lines.append(f"| {ch} | +{delta} B |")
lines += [
    "",
    "The four-envelope array capacity adds the persistent SBR allocation shown above. The dry writer's local scratch buffer grows from 1024 to 2048 bytes to cover a conservative 2-channel × 4-envelope × 64-band × 20-bit bound plus fixed fields; that is +1024 bytes of call-frame scratch, not persistent encoder state. The 196-stream identity matrix and LC/HE decoder gates are linked in the tier17 production-final archive. No timing claim is made.",
    "",
    "The source patch and GCC13 build outputs, including section map, build log, library hash, allocation model output, and staged source hashes, are adjacent to this file and summarized in `clean-tier17-footprint.json`.",
    "",
]
(TIER / "CLEAN-FOOTPRINT.md").write_text("\n".join(lines))
print(TIER / "CLEAN-FOOTPRINT.md")
