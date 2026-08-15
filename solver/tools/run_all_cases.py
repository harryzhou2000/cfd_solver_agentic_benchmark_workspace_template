#!/usr/bin/env python3
"""Production run orchestrator for the CFD solver agentic benchmark.

Runs every required benchmark case with `mpirun -np <N>`, captures solver
stdout to <output_dir>/stdout.log, then validates each case directory with
the examiner contract validator and reports pass/fail plus wall-clock time
from run_status.json.

Usage:
    python3 run_all_cases.py [--np N] [--cases id1,id2,...] SOLVER_EXE OUTPUT_ROOT

Examples:
    python3 run_all_cases.py solver/build/cfd_solver solver/results
    python3 run_all_cases.py --np 4 --cases naca0012_m200_inviscid \
        solver/build/cfd_solver solver/results
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent

# The eight required benchmark cases (production order: steady first).
STEADY_CASES = [
    "naca0012_m015_inviscid",
    "naca0012_m080_inviscid",
    "naca0012_m200_inviscid",
    "naca0012_m015_laminar_re5000",
    "naca0012_m080_laminar_re5000",
    "naca0012_m200_laminar_re5000",
    "cylinder_m010_laminar_re20",
]
TRANSIENT_CASES = ["cylinder_m010_laminar_re200"]
ALL_CASES = STEADY_CASES + TRANSIENT_CASES

CASES_DIR = REPO_ROOT / "cfd_solver_agentic_benchmark" / "inputs" / "cases"
EXAMINER = (
    REPO_ROOT
    / "cfd_solver_agentic_benchmark"
    / "examiner"
    / "validate_outputs.py"
)
DEFAULT_PYTHON = REPO_ROOT / "solver" / ".venv" / "bin" / "python3"


def case_file(case_id: str) -> Path:
    path = CASES_DIR / f"{case_id}.json"
    if not path.exists():
        raise SystemExit(f"case file not found: {path}")
    return path


def pick_python() -> str:
    if DEFAULT_PYTHON.exists():
        return str(DEFAULT_PYTHON)
    return sys.executable if sys.executable else "python3"


def run_solver(
    solver_exe: Path, case_id: str, output_dir: Path, np: int
) -> tuple[int, float]:
    """Run one case with mpirun; returns (exit_code, wall_time_seconds)."""
    output_dir.mkdir(parents=True, exist_ok=True)
    log_path = output_dir / "stdout.log"
    cmd = [
        "mpirun",
        "-np",
        str(np),
        str(solver_exe),
        "solve",
        "--case",
        str(case_file(case_id)),
        "--output",
        str(output_dir),
    ]
    print(f"=== {case_id} ===")
    print(f"  cmd: {' '.join(cmd)}")
    with log_path.open("wb") as log:
        proc = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT)
    print(f"  exit code: {proc.returncode}  (stdout -> {log_path})")

    wall = 0.0
    status_path = output_dir / "run_status.json"
    if status_path.exists():
        try:
            status = json.loads(status_path.read_text())
            wall = float(status.get("wall_time_seconds", 0.0))
            print(
                f"  wall time: {wall:.1f} s | final_step="
                f"{status.get('final_step')} | status="
                f"{status.get('convergence_status')}"
            )
        except (json.JSONDecodeError, TypeError, ValueError):
            print(f"  WARNING: could not parse {status_path}")
    return proc.returncode, wall


def validate(output_dir: Path, python: str) -> tuple[int, str]:
    """Run the examiner validator; returns (exit_code, output)."""
    cmd = [python, str(EXAMINER), str(output_dir)]
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
    except subprocess.TimeoutExpired:
        return 2, "validator timed out"
    return proc.returncode, (proc.stdout + proc.stderr).strip()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("solver_exe", type=Path, help="path to cfd_solver binary")
    parser.add_argument("output_root", type=Path, help="results root directory")
    parser.add_argument("--np", type=int, default=4, help="MPI ranks per run (default 4)")
    parser.add_argument(
        "--cases",
        default=",".join(ALL_CASES),
        help="comma-separated case ids to run (default: all 8)",
    )
    parser.add_argument(
        "--python",
        default=None,
        help="python interpreter for the examiner (default: solver/.venv/bin/python3)",
    )
    args = parser.parse_args()

    solver_exe = args.solver_exe.resolve()
    output_root = args.output_root.resolve()
    if not solver_exe.exists():
        raise SystemExit(f"solver executable not found: {solver_exe}")

    requested = [c.strip() for c in args.cases.split(",") if c.strip()]
    unknown = [c for c in requested if c not in ALL_CASES]
    if unknown:
        raise SystemExit(f"unknown case id(s): {unknown}")
    cases = requested

    python = args.python or pick_python()
    if not os.path.exists(python):
        python = "python3"
    print(f"solver: {solver_exe}")
    print(f"output root: {output_root}")
    print(f"mpirun -np {args.np}  cases: {', '.join(cases)}")
    print(f"examiner python: {python}")

    results: list[tuple[str, bool, float, str]] = []
    for case_id in cases:
        output_dir = output_root / case_id
        if output_dir.exists() and (output_dir / "run_status.json").exists():
            print(f"\n[skip] {case_id}: output dir already has run_status.json")
            rc = 0
            wall = 0.0
            status_path = output_dir / "run_status.json"
            try:
                status = json.loads(status_path.read_text())
                wall = float(status.get("wall_time_seconds", 0.0))
            except Exception:
                pass
        else:
            rc, wall = run_solver(solver_exe, case_id, output_dir, args.np)

        vrc, vout = validate(output_dir, python)
        ok = rc == 0 and vrc == 0
        results.append((case_id, ok, wall, vout))
        print(f"  examiner {'PASS' if vrc == 0 else 'FAIL'} ({vrc})")
        if vrc != 0:
            print(f"  validator output:\n{vout}")

    print("\n================ SUMMARY ================")
    n_pass = 0
    for case_id, ok, wall, _ in results:
        flag = "PASS" if ok else "FAIL"
        if ok:
            n_pass += 1
        print(f"  [{flag}] {case_id:32s} wall={wall:10.1f} s")
    print(f"  passed {n_pass}/{len(results)}")
    return 0 if n_pass == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
