#!/usr/bin/env python3
"""Launch the benchmark cases through the documented SaturnCFD CLI."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys


CASE_ORDER = [
    "naca0012_m015_inviscid",
    "naca0012_m080_inviscid",
    "naca0012_m200_inviscid",
    "naca0012_m015_laminar_re5000",
    "naca0012_m080_laminar_re5000",
    "naca0012_m200_laminar_re5000",
    "cylinder_m010_laminar_re20",
    "cylinder_m010_laminar_re200",
]


def parse_args() -> argparse.Namespace:
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=("production", "rank-validation"), default="production")
    parser.add_argument("--solver", type=Path, default=root / "solver/build/saturn_cfd")
    parser.add_argument("--benchmark", type=Path, default=root / "cfd_solver_agentic_benchmark")
    parser.add_argument("--results", type=Path, default=root / "solver/results")
    parser.add_argument("--steady-ranks", type=int, default=8)
    parser.add_argument("--transient-ranks", type=int, default=4)
    parser.add_argument("--force", action="store_true", help="rerun an already completed output")
    parser.add_argument("--case", action="append", choices=CASE_ORDER, help="restrict production to selected cases")
    return parser.parse_args()


def completed(output: Path) -> bool:
    try:
        metadata = json.loads((output / "metadata.json").read_text())
    except (OSError, json.JSONDecodeError):
        return False
    return metadata.get("completed") is True and metadata.get("convergence_status") in {
        "converged",
        "statistically_periodic",
    }


def launch(solver: Path, case_file: Path, output: Path, ranks: int) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    command = [
        "mpirun", "-np", str(ranks), str(solver), "solve",
        "--case", str(case_file), "--output", str(output), "--report-level", "full",
    ]
    print("+", " ".join(command), flush=True)
    environment = os.environ.copy()
    process = subprocess.run(command, env=environment, check=False)
    if process.returncode != 0:
        raise RuntimeError(f"solver failed with exit code {process.returncode}: {case_file.stem}")


def main() -> int:
    args = parse_args()
    if not args.solver.is_file():
        raise FileNotFoundError(f"build the solver first: {args.solver}")
    case_dir = args.benchmark / "inputs/cases"
    if args.mode == "production":
        selected = args.case or CASE_ORDER
        for case_id in selected:
            case_file = case_dir / f"{case_id}.json"
            output = args.results / case_id
            if completed(output) and not args.force:
                print(f"skip completed {case_id}")
                continue
            ranks = args.transient_ranks if case_id == "cylinder_m010_laminar_re200" else args.steady_ranks
            launch(args.solver, case_file, output, ranks)
    else:
        validation = ["naca0012_m015_inviscid", "cylinder_m010_laminar_re20"]
        for case_id in validation:
            for ranks in (1, 2, 8):
                output = args.results / "rank_validation" / f"{case_id}_np{ranks}"
                if completed(output) and not args.force:
                    print(f"skip completed {output.name}")
                    continue
                launch(args.solver, case_dir / f"{case_id}.json", output, ranks)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:  # clear orchestration failure without hiding the solver status
        print(f"run_cases.py: {error}", file=sys.stderr)
        raise SystemExit(1)
