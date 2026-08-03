#!/usr/bin/env python3
"""Generate report/run_manifest.csv from run_status.json + forces.csv.

Columns:
    case_id, command, mpi_ranks, wall_time_seconds, convergence_status,
    residual_reduction, final_cl, final_cd, notes

Usage:
    python generate_run_manifest.py [results_dir] [out_csv]
"""

import csv
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from plot_utils import read_csv  # noqa: E402

CASES = [
    "naca0012_m015_inviscid", "naca0012_m080_inviscid", "naca0012_m200_inviscid",
    "naca0012_m015_laminar_re5000", "naca0012_m080_laminar_re5000",
    "naca0012_m200_laminar_re5000",
    "cylinder_m010_laminar_re20", "cylinder_m010_laminar_re200",
]


def _residual_reduction(case_dir, status):
    """Orders of residual reduction from run_status, else from residuals.csv."""
    val = status.get("residual_reduction_orders")
    if val is not None:
        return val
    data = read_csv(os.path.join(case_dir, "residuals.csv"))
    if data is None:
        return None
    r = data["residual_l2"]
    r = r[np.isfinite(r) & (r > 0.0)]
    if len(r) < 2:
        return None
    return float(np.log10(r[0] / r[-1]))


def main():
    tools_dir = os.path.dirname(os.path.abspath(__file__))
    solver_dir = os.path.dirname(tools_dir)
    results_dir = (sys.argv[1] if len(sys.argv) > 1
                   else os.path.join(solver_dir, "results"))
    out_csv = (sys.argv[2] if len(sys.argv) > 2
               else os.path.join(solver_dir, "report", "run_manifest.csv"))

    rows = []
    for case in CASES:
        case_dir = os.path.join(results_dir, case)
        status_path = os.path.join(case_dir, "run_status.json")
        if not os.path.isfile(status_path):
            continue
        with open(status_path) as f:
            status = json.load(f)

        forces = read_csv(os.path.join(case_dir, "forces.csv"))
        final_cl = final_cd = ""
        if forces is not None and len(forces) > 0:
            final_cl = f"{forces['cl'][-1]:.6g}"
            final_cd = f"{forces['cd'][-1]:.6g}"

        red = _residual_reduction(case_dir, status)
        rows.append([
            case,
            status.get("command", ""),
            status.get("mpi_ranks", ""),
            status.get("wall_time_seconds", ""),
            status.get("convergence_status", ""),
            f"{red:.3f}" if red is not None else "",
            final_cl,
            final_cd,
            status.get("notes", ""),
        ])

    os.makedirs(os.path.dirname(out_csv), exist_ok=True)
    with open(out_csv, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["case_id", "command", "mpi_ranks", "wall_time_seconds",
                    "convergence_status", "residual_reduction", "final_cl",
                    "final_cd", "notes"])
        w.writerows(rows)
    print(f"wrote {out_csv} with {len(rows)} runs")


if __name__ == "__main__":
    main()
