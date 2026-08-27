#!/usr/bin/env python3
"""Builds report/run_manifest.csv (and .md) from the submitted case outputs."""

from __future__ import annotations

import argparse
import csv
import json
import os

FIELDS = ["case_id", "command", "mpi_ranks", "steps", "final_physical_time",
          "residual_reduction_orders", "wall_time_seconds", "cl", "cd",
          "convergence_status", "solver_version", "git_revision", "inviscid_flux",
          "reconstruction", "limiter", "shock_fix_strength", "notes"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--results", default="results")
    ap.add_argument("--extra", nargs="*", default=[],
                    help="extra result directories (e.g. MPI rank studies)")
    ap.add_argument("--out", default="report/run_manifest.csv")
    args = ap.parse_args()

    dirs = [os.path.join(args.results, d) for d in sorted(os.listdir(args.results))
            if os.path.isdir(os.path.join(args.results, d)) and not d.startswith("_")]
    # Label the extra study runs by their parent directory so the manifest
    # distinguishes e.g. studies/mpi/<case>_np2 from the production run.
    dirs += args.extra
    rows = []
    for d in dirs:
        mp = os.path.join(d, "metadata.json")
        sp = os.path.join(d, "run_status.json")
        if not (os.path.exists(mp) and os.path.exists(sp)):
            continue
        m = json.load(open(mp))
        s = json.load(open(sp))
        rows.append({
            "case_id": os.path.relpath(d).rstrip("/"),
            "command": s["command"],
            "mpi_ranks": s["mpi_ranks"],
            "steps": s["final_step"],
            "final_physical_time": s["final_physical_time"],
            "residual_reduction_orders": round(float(s["residual_reduction_orders"]), 3),
            "wall_time_seconds": round(float(s["wall_time_seconds"]), 1),
            "cl": float(m.get("final_cl") or 0.0),
            "cd": float(m.get("final_cd") or 0.0),
            "convergence_status": s["convergence_status"],
            # Recorded per run so the manifest itself shows that every submitted
            # result came from the same build with the same numerics.
            "solver_version": m.get("solver_version", ""),
            "git_revision": m.get("git_revision", ""),
            "inviscid_flux": m.get("inviscid_flux", ""),
            "reconstruction": m.get("reconstruction", ""),
            "limiter": m.get("limiter", ""),
            "shock_fix_strength": m.get("shock_fix_strength", ""),
            "notes": s["notes"],
        })
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=FIELDS)
        w.writeheader()
        w.writerows(rows)
    md = os.path.splitext(args.out)[0] + ".md"
    with open(md, "w") as f:
        f.write("# Run Manifest\n\nEvery row is one solver invocation whose outputs are "
                "included in this submission.\n\n")
        f.write("| case | ranks | steps | t_final | residual orders | wall time [s] | C_L | C_D "
                "| status |\n")
        f.write("|---|---:|---:|---:|---:|---:|---:|---:|---|\n")
        for r in rows:
            f.write(f"| `{r['case_id']}` | {r['mpi_ranks']} | {r['steps']} | "
                    f"{r['final_physical_time']:.3f} | {r['residual_reduction_orders']:.2f} | "
                    f"{r['wall_time_seconds']:.1f} | {r['cl']:.6f} | {r['cd']:.6f} | "
                    f"{r['convergence_status']} |\n")
        f.write("\n## Exact commands\n\n")
        for r in rows:
            f.write(f"* `{r['case_id']}`\n\n      {r['command']}\n\n")
        f.write("\n## Notes\n\n")
        for r in rows:
            f.write(f"* `{r['case_id']}`: {r['notes']}\n")
    print(f"wrote {args.out} and {md} ({len(rows)} runs)")


if __name__ == "__main__":
    main()
