#!/usr/bin/env python3
"""Generate run_manifest.csv from result directories."""
import json, csv
from pathlib import Path

SOLVER_DIR = Path(__file__).parent.parent
RESULTS = SOLVER_DIR / "results"
manifest_path = SOLVER_DIR / "report" / "run_manifest.csv"
manifest_path.parent.mkdir(parents=True, exist_ok=True)

rows = []
for case_dir in sorted(RESULTS.iterdir()):
    if not case_dir.is_dir(): continue
    case_id = case_dir.name
    meta_p = case_dir / "metadata.json"
    status_p = case_dir / "run_status.json"
    if not meta_p.exists() or not status_p.exists(): continue
    meta = json.loads(meta_p.read_text())
    status = json.loads(status_p.read_text())
    rows.append({
        "case_id": case_id,
        "mpi_ranks": meta.get("mpi_ranks", 0),
        "wall_time_seconds": round(status.get("wall_time_seconds", 0), 2),
        "final_step": status.get("final_step", 0),
        "final_physical_time": status.get("final_physical_time", 0),
        "convergence_status": status.get("convergence_status", "unknown"),
        "residual_reduction_orders": round(status.get("residual_reduction_orders", 0), 2),
        "num_cells_global": meta.get("num_cells_global", 0),
        "command": status.get("command", ""),
    })

with open(manifest_path, 'w', newline='') as f:
    w = csv.DictWriter(f, fieldnames=["case_id","mpi_ranks","wall_time_seconds","final_step",
        "final_physical_time","convergence_status","residual_reduction_orders","num_cells_global","command"])
    w.writeheader()
    for r in rows: w.writerow(r)
print(f"run_manifest.csv written to {manifest_path}")
