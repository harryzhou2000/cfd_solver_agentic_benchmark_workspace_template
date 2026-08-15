"""Generate report/run_manifest.csv and rank-count consistency summary.

Usage: make_manifest.py <results_root> <consistency_root> <report_dir>
"""
from __future__ import annotations

import csv
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from cfdpost import col, read_csv_rows


def summarize(d: Path):
    meta = json.loads((d / "metadata.json").read_text())
    status = json.loads((d / "run_status.json").read_text())
    forces = read_csv_rows(d / "forces.csv")
    cmd = status["command"]
    if not cmd.startswith("mpirun"):
        cmd = f"mpirun -np {status['mpi_ranks']} {cmd}"
    return {
        "case_id": meta["case_id"],
        "command": cmd,
        "mpi_ranks": status["mpi_ranks"],
        "wall_time_seconds": round(status["wall_time_seconds"], 1),
        "final_step": status["final_step"],
        "final_physical_time": status["final_physical_time"],
        "convergence_status": status["convergence_status"],
        "residual_reduction_orders": round(status["residual_reduction_orders"], 3),
        "cl": forces[-1]["cl"],
        "cd": forces[-1]["cd"],
        "notes": status["notes"],
    }


def main():
    results = Path(sys.argv[1])
    consistency = Path(sys.argv[2])
    report = Path(sys.argv[3])
    rows = []
    for d in sorted(results.iterdir()):
        if d.is_dir() and (d / "metadata.json").exists():
            rows.append(summarize(d))
    with open(report / "run_manifest.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)

    # rank consistency table (include the production np=8 runs)
    cons = {}
    for d in sorted(consistency.iterdir()):
        if d.is_dir() and (d / "metadata.json").exists():
            s = summarize(d)
            cons.setdefault(s["case_id"], []).append(s)
    for d in sorted(results.iterdir()):
        if d.is_dir() and (d / "metadata.json").exists():
            s = summarize(d)
            if s["case_id"] in cons:
                cons[s["case_id"]].append(s)
    lines = ["# MPI rank-count consistency summary", ""]
    for cid, entries in cons.items():
        entries.sort(key=lambda e: e["mpi_ranks"])
        lines.append(f"## {cid}")
        lines.append("ranks | wall_time_s | final cl | final cd | residual_orders | speedup")
        lines.append("---|---|---|---|---|---")
        t1 = None
        for e in entries:
            if t1 is None:
                t1 = e["wall_time_seconds"]
            speed = t1 / e["wall_time_seconds"] if e["wall_time_seconds"] else 0
            lines.append(
                f"{e['mpi_ranks']} | {e['wall_time_seconds']} | {e['cl']} | {e['cd']} | "
                f"{e['residual_reduction_orders']} | {speed:.2f}"
            )
        lines.append("")
    (report / "rank_consistency.md").write_text("\n".join(lines))
    print("wrote run_manifest.csv and rank_consistency.md")


if __name__ == "__main__":
    main()
