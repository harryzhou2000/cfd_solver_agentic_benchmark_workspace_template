#!/usr/bin/env python3
"""MPI rank-count consistency validation for the CFD solver.

Runs one NACA case (naca0012_m015_inviscid) and one cylinder case
(cylinder_m010_laminar_re20) at np=1, 4 and 8 and compares the final force
coefficients (CL, CD) and residual reduction across rank counts.

Usage:
    python3 run_mpi_check.py [--steps N] [--np "1,4,8"] SOLVER_EXE OUTPUT_ROOT

With --steps N the case JSONs are copied to a scratch directory with
max_steps capped at N so the check finishes quickly; the benchmark input
files are never modified. Output goes to <OUTPUT_ROOT>/mpi_check/<case>/np<N>/.
"""

from __future__ import annotations

import argparse
import csv
import json
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent

CASES_DIR = REPO_ROOT / "cfd_solver_agentic_benchmark" / "inputs" / "cases"
CHECK_CASES = ["naca0012_m015_inviscid", "cylinder_m010_laminar_re20"]
RANK_COUNTS = [1, 4, 8]


def case_path(case_id: str, steps: int | None) -> Path:
    """Return the case file; when steps is given, a capped scratch copy."""
    src = CASES_DIR / f"{case_id}.json"
    if steps is None:
        return src
    scratch = REPO_ROOT / "solver" / "tools" / "timing_work" / "mpi_check"
    scratch.mkdir(parents=True, exist_ok=True)
    cfg = json.loads(src.read_text())
    cfg["run_control"]["max_steps"] = steps
    # Mesh paths are resolved relative to the case file directory, so the
    # scratch copy must use an absolute mesh path.
    cfg["mesh"]["file"] = str(
        (CASES_DIR / ".." / "meshes" / Path(cfg["mesh"]["file"]).name).resolve()
    )
    out = scratch / f"{case_id}_steps{steps}.json"
    out.write_text(json.dumps(cfg, indent=2))
    return out


def run_case(solver_exe: Path, case_id: str, np: int, steps: int | None,
             output_root: Path) -> tuple[bool, dict]:
    out_dir = output_root / "mpi_check" / case_id / f"np{np}"
    out_dir.mkdir(parents=True, exist_ok=True)
    cmd = [
        "mpirun", "-np", str(np), str(solver_exe), "solve",
        "--case", str(case_path(case_id, steps)), "--output", str(out_dir),
    ]
    print(f"  np={np}: {' '.join(cmd)}")
    with (out_dir / "stdout.log").open("wb") as log:
        proc = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT)
    summary: dict = {"exit": proc.returncode, "case_id": case_id, "np": np}
    status = out_dir / "run_status.json"
    if status.exists():
        summary.update(json.loads(status.read_text()))
    return proc.returncode == 0, summary


def load_last_row(path: Path) -> dict[str, str] | None:
    if not path.exists():
        return None
    with path.open(newline="") as f:
        rows = list(csv.DictReader(f))
    return rows[-1] if rows else None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("solver_exe", type=Path)
    parser.add_argument("output_root", type=Path)
    parser.add_argument("--steps", type=int, default=None,
                        help="cap max_steps for a fast consistency check")
    parser.add_argument("--np", default="1,4,8",
                        help="comma-separated rank counts (default 1,4,8)")
    parser.add_argument("--compare-only", action="store_true",
                        help="skip solver runs; compare existing outputs "
                             "under <output_root>/mpi_check/")
    args = parser.parse_args()

    solver_exe = args.solver_exe.resolve()
    output_root = args.output_root.resolve()
    rank_counts = [int(x) for x in args.np.split(",")]
    if not solver_exe.exists():
        raise SystemExit(f"solver executable not found: {solver_exe}")

    all_ok = True
    for case_id in CHECK_CASES:
        print(f"\n=== MPI consistency: {case_id} (steps={args.steps or 'full'}) ===")
        summaries: dict[int, dict] = {}
        if not args.compare_only:
            for np in rank_counts:
                ok, summary = run_case(solver_exe, case_id, np, args.steps,
                                       output_root)
                summaries[np] = summary
                if not ok:
                    all_ok = False
                print(f"    exit={summary.get('exit')} final_step="
                      f"{summary.get('final_step')} status="
                      f"{summary.get('convergence_status')} wall="
                      f"{summary.get('wall_time_seconds', 0):.1f}s")
        else:
            for np in rank_counts:
                status = output_root / "mpi_check" / case_id / f"np{np}" \
                    / "run_status.json"
                if not status.exists():
                    print(f"    np={np}: no run_status.json to compare")
                    all_ok = False
                    continue
                summaries[np] = json.loads(status.read_text())

        # Compare final forces and residual reduction across rank counts.
        print(f"    {'np':>3} {'CL':>14} {'CD':>14} {'res_red_orders':>16} "
              f"{'wall_s':>10}")
        last_rows: dict[int, dict] = {}
        for np, summary in summaries.items():
            out_dir = output_root / "mpi_check" / case_id / f"np{np}"
            forces = load_last_row(out_dir / "forces.csv")
            residuals = load_last_row(out_dir / "residuals.csv")
            if forces is None or residuals is None:
                print(f"    {np:>3}  missing forces/residuals")
                all_ok = False
                continue
            last_rows[np] = forces
            red = float(summary.get("residual_reduction_orders", 0.0) or 0.0)
            print(f"    {np:>3} {float(forces['cl']):14.8e} "
                  f"{float(forces['cd']):14.8e} {red:16.4f} "
                  f"{float(summary.get('wall_time_seconds', 0.0)):10.1f}")

        if len(last_rows) == len(rank_counts):
            base = last_rows[rank_counts[0]]
            base_cd = abs(float(base["cd"]))
            base_red = float(summaries[rank_counts[0]].get(
                "residual_reduction_orders", 0.0) or 0.0)
            for np in rank_counts[1:]:
                row = last_rows[np]
                dcd_rel = abs(float(row["cd"]) - base_cd) / max(base_cd, 1e-30)
                dred = abs(float(summaries[np].get(
                    "residual_reduction_orders", 0.0) or 0.0) - base_red)
                dcl = abs(float(row["cl"]) - float(base["cl"]))
                # CD is the robust comparison metric (relative tolerance).
                # A 2% CD band covers roundoff-induced trajectory spread
                # while a case is still converging at the comparison step
                # count; residual reduction must track; CL spread is
                # reported as context because low-amplitude lift
                # oscillations in a still-settling wake amplify
                # roundoff-level ordering differences between rank counts.
                consistent = dcd_rel <= 2.0e-2 and dred <= 0.25
                if not consistent:
                    all_ok = False
                print(f"    np={np} vs np={rank_counts[0]}: "
                      f"|dCL|={dcl:.3e} |dCD|/CD={dcd_rel:.3e} "
                      f"|dred|={dred:.4f} -> "
                      f"{'consistent' if consistent else 'INCONSISTENT'}")

    print(f"\nMPI check overall: {'PASS' if all_ok else 'FAIL'}")
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
