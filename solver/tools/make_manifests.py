#!/usr/bin/env python3
"""Generate report/run_manifest.md and report/figure_manifest.csv."""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path

from cfd_io import read_json


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("results_dir", type=Path)
    ap.add_argument("--report-dir", type=Path,
                    default=Path(__file__).resolve().parent.parent / "report")
    args = ap.parse_args()
    results_dir = args.results_dir.resolve()
    report_dir = args.report_dir.resolve()
    report_dir.mkdir(parents=True, exist_ok=True)

    with open(report_dir / "run_manifest.md", "w") as md:
        md.write("# Run Manifest\n\n")
        md.write("| case | ranks | steps | physical time | wall time (s) | "
                 "residual orders | status | notes | command |\n")
        md.write("|---|---|---|---|---|---|---|---|---|\n")
        for case_dir in sorted(results_dir.iterdir()):
            if not case_dir.name.startswith("final_"):
                continue
            status_path = case_dir / "run_status.json"
            if not status_path.exists():
                continue
            s = read_json(status_path)
            md.write(
                f"| {s['case_id']} | {s['mpi_ranks']} | {s['final_step']} | "
                f"{s['final_physical_time']:.3f} | "
                f"{s['wall_time_seconds']:.1f} | "
                f"{s['residual_reduction_orders']:.2f} | "
                f"{s['convergence_status']} | {s.get('notes', '')} | "
                f"`{s['command']}` |\n"
            )

    figures = []
    for case_dir in sorted(results_dir.iterdir()):
        if not case_dir.name.startswith("final_"):
            continue
        if not (case_dir / "run_status.json").exists():
            continue
        metadata = read_json(case_dir / "metadata.json")
        case_id = metadata.get("case_id", case_dir.name)
        for name, var, src, caption in [
            (f"{case_id}_mach.png", "mach", "field_final.vtu",
             "Mach number field"),
            (f"{case_id}_pressure.png", "pressure", "field_final.vtu",
             "static pressure field"),
            (f"{case_id}_residual.png", "residual_l2", "residuals.csv",
             "component and total L2 residual history"),
            (f"{case_id}_forces.png", "cl,cd", "forces.csv",
             "lift and drag coefficient history"),
            (f"{case_id}_cp.png", "cp", "surface.csv",
             "wall pressure coefficient"),
        ]:
            figures.append(
                [name, case_id, "field" if var in ("mach", "pressure")
                 else "line", var, src, caption]
            )
        if (report_dir / "figures" / f"{case_id}_vorticity.png").exists():
            figures.append(
                [f"{case_id}_vorticity.png", case_id, "field", "vorticity",
                 "field_final.vtu", "vorticity field (clipped)"]
            )
        if (report_dir / "figures" / f"{case_id}_cf.png").exists():
            figures.append(
                [f"{case_id}_cf.png", case_id, "line", "cf", "surface.csv",
                 "wall skin-friction coefficient"]
            )
    with open(report_dir / "figure_manifest.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["figure_file", "case_id", "figure_type", "variable",
                    "source_file", "caption"])
        w.writerows(figures)
    print(f"wrote {report_dir / 'run_manifest.md'} and "
          f"{report_dir / 'figure_manifest.csv'}")


if __name__ == "__main__":
    main()
