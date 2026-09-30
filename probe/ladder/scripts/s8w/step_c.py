#!/usr/bin/env python3
import json
import pathlib

SCRIPT_DIR = pathlib.Path(__file__).resolve().parent
LADDER_DIR = SCRIPT_DIR.parents[1]

res = {
    "status": "stopped",
    "reason": "Step A1 alignment threshold (>= 95% window match) was not reached for FDK (82.68%) or Apple (80.62%). Per Step A1 prompt rule: 'If no combination reaches 95 %, STOP, report the table and what you found, and do not run step B.' Step C is likewise stopped."
}

out_dir = LADDER_DIR / "results/s8w"
out_dir.mkdir(parents=True, exist_ok=True)
(out_dir / "step_c.json").write_text(json.dumps(res, indent=2))
print("Step C stopped per A1 stop condition. Wrote probe/ladder/results/s8w/step_c.json")
