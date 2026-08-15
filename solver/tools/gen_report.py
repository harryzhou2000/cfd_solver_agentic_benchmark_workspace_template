#!/usr/bin/env python3
"""Generate results_table.tex for the LaTeX report."""
import json, csv
from pathlib import Path

SOLVER_DIR = Path(__file__).parent.parent
RESULTS = SOLVER_DIR / "results"

rows = []
for case_dir in sorted(RESULTS.iterdir()):
    if not case_dir.is_dir(): continue
    case_id = case_dir.name
    meta_p = case_dir / "metadata.json"
    status_p = case_dir / "run_status.json"
    forces_p = case_dir / "forces.csv"
    if not meta_p.exists(): continue
    meta = json.loads(meta_p.read_text())
    status = json.loads(status_p.read_text()) if status_p.exists() else {}
    # get final forces
    cd = cl = "N/A"
    if forces_p.exists():
        with open(forces_p) as f:
            lines = f.readlines()
        if len(lines) > 1:
            vals = lines[-1].strip().split(',')
            cl = f"{float(vals[2]):.4f}"
            cd = f"{float(vals[3]):.4f}"
    ranks = meta.get("mpi_ranks", 0)
    step = status.get("final_step", 0)
    conv = status.get("convergence_status", meta.get("convergence_status", "unknown"))
    rows.append(f"{case_id} & {ranks} & {step} & {cd} & {conv} \\\\")

out = SOLVER_DIR / "report" / "results_table.tex"
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text("\n".join(rows) + "\n")
print(f"results_table.tex written with {len(rows)} rows")
