#!/usr/bin/env python3
"""Assemble report/run_manifest.csv from result dirs.

Usage: .venv/bin/python tools/make_manifest.py --results results --out report/run_manifest.csv
"""
import argparse
import csv
import json
from pathlib import Path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--results", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    rows = []
    for cd in sorted(Path(args.results).iterdir()):
        if not (cd / "metadata.json").exists():
            continue
        meta = json.loads((cd / "metadata.json").read_text())
        status = json.loads((cd / "run_status.json").read_text())
        rows.append(
            {
                "case_id": cd.name,
                "command": status["command"],
                "mpi_ranks": status["mpi_ranks"],
                "wall_time_seconds": f"{status['wall_time_seconds']:.1f}",
                "final_step": status["final_step"],
                "final_physical_time": status["final_physical_time"],
                "convergence_status": status["convergence_status"],
                "residual_reduction_orders": f"{status['residual_reduction_orders']:.2f}",
                "notes": status["notes"],
            }
        )
    with open(args.out, "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)
    print(f"wrote {args.out} ({len(rows)} cases)")


if __name__ == "__main__":
    main()
