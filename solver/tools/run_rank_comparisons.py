#!/usr/bin/env python3
"""Run and validate small MPI-rank comparisons without invoking a shell.

The default matrix is the NACA Mach 2.0 inviscid and cylinder Re20 cases at
two and eight ranks.  Each solve is written below
``solver/results/rank_comparisons/<case-id>/np<ranks>`` and is validated with
the benchmark's structural output validator before its summary is added to
``comparison.csv``.

The runner intentionally refuses to start while a Re200 production solve is
visible in the process table, and it never deletes or overwrites an existing
comparison directory.
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import shlex
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_CASES = (
    "naca0012_m200_inviscid",
    "cylinder_m010_laminar_re20",
)

COMPARISON_COLUMNS = (
    "case_id",
    "ranks",
    "command",
    "wall_time_seconds",
    "solver_wall_time_seconds",
    "launch_returncode",
    "validator_command",
    "validator_returncode",
    "validation_passed",
    "convergence_status",
    "final_step",
    "final_physical_time",
    "residual_l2",
    "residual_linf",
    "cl",
    "cd",
    "cmz",
    "pressure_drag",
    "viscous_drag",
    "pressure_lift",
    "viscous_lift",
    "partition_edge_cut",
    "owned_cells_min",
    "owned_cells_max",
    "owned_cells_mean",
)


class ComparisonError(RuntimeError):
    """Raised when a requested comparison cannot be run safely."""


@dataclass(frozen=True)
class RunRequest:
    case_id: str
    ranks: int
    case_file: Path
    output_dir: Path
    command: tuple[str, ...]


def case_path(case_id: str) -> Path:
    return REPOSITORY_ROOT / "cfd_solver_agentic_benchmark" / "inputs" / "cases" / f"{case_id}.json"


def re200_job_active(process_listing: str | None = None) -> bool:
    """Return whether a Re200 MPI/solver command is currently running.

    The optional listing makes this small guard deterministic in unit tests.
    """
    detailed_listing = process_listing is None
    if detailed_listing:
        completed = subprocess.run(
            ["ps", "-eo", "comm=,args="], check=False, capture_output=True, text=True
        )
        process_listing = completed.stdout
    for command in process_listing.splitlines():
        lowered = command.lower()
        if detailed_listing:
            fields = lowered.split(None, 1)
            executable = Path(fields[0]).name if fields else ""
            if executable not in {"cfd_solver", "mpirun", "mpiexec", "orterun"}:
                continue
            repository_marker = str(REPOSITORY_ROOT).lower()
            own_solver_marker = "solver/build/cfd_solver"
            own_output_marker = "solver/results/cylinder_m010_laminar_re200"
            if not any(marker in lowered for marker in
                       (repository_marker, own_solver_marker, own_output_marker)):
                continue
        if "re200" in lowered and ("cfd_solver" in lowered or "mpirun" in lowered or "mpiexec" in lowered):
            return True
    return False


def make_request(
    *,
    case_id: str,
    ranks: int,
    solver: Path,
    mpi_launcher: str,
    output_root: Path,
) -> RunRequest:
    if ranks <= 0:
        raise ComparisonError(f"MPI rank count must be positive, got {ranks}")
    input_case = case_path(case_id)
    if not input_case.is_file():
        raise ComparisonError(f"case file does not exist: {input_case}")
    output_dir = output_root / case_id / f"np{ranks}"
    command = (
        mpi_launcher,
        "-np",
        str(ranks),
        str(solver),
        "solve",
        "--case",
        str(input_case),
        "--output",
        str(output_dir),
        "--report-level",
        "full",
    )
    return RunRequest(case_id, ranks, input_case, output_dir, command)


def read_last_csv_row(path: Path) -> dict[str, str]:
    with path.open(newline="") as stream:
        rows = list(csv.DictReader(stream))
    if not rows:
        raise ComparisonError(f"no data rows in {path}")
    return rows[-1]


def parse_summary(output_dir: Path) -> dict[str, str | float | int]:
    """Extract the rank-comparison values from one completed output directory."""
    metadata = json.loads((output_dir / "metadata.json").read_text())
    status = json.loads((output_dir / "run_status.json").read_text())
    residual = read_last_csv_row(output_dir / "residuals.csv")
    forces = read_last_csv_row(output_dir / "forces.csv")
    with (output_dir / "partition_diagnostics.csv").open(newline="") as stream:
        partitions = list(csv.DictReader(stream))
    if not partitions:
        raise ComparisonError(f"no partition rows in {output_dir / 'partition_diagnostics.csv'}")
    owned = [int(row["num_cells_owned"]) for row in partitions]
    return {
        "solver_wall_time_seconds": float(status["wall_time_seconds"]),
        "convergence_status": str(status["convergence_status"]),
        "final_step": int(status["final_step"]),
        "final_physical_time": float(status["final_physical_time"]),
        "residual_l2": float(residual["residual_l2"]),
        "residual_linf": float(residual["residual_linf"]),
        "cl": float(forces["cl"]),
        "cd": float(forces["cd"]),
        "cmz": float(forces["cmz"]),
        "pressure_drag": float(forces["pressure_drag"]),
        "viscous_drag": float(forces["viscous_drag"]),
        "pressure_lift": float(forces["pressure_lift"]),
        "viscous_lift": float(forces["viscous_lift"]),
        "partition_edge_cut": int(metadata["partition_edge_cut"]),
        "owned_cells_min": min(owned),
        "owned_cells_max": max(owned),
        "owned_cells_mean": sum(owned) / len(owned),
    }


def write_comparison_csv(path: Path, rows: Iterable[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=COMPARISON_COLUMNS)
        writer.writeheader()
        for row in rows:
            writer.writerow({column: row.get(column, "") for column in COMPARISON_COLUMNS})


def run_one(request: RunRequest, validator: Path) -> dict[str, object]:
    """Launch one solve, validate it, and return a CSV-ready row.

    This function deliberately leaves a failed output directory intact for
    diagnosis rather than trying to clean it up.
    """
    if re200_job_active():
        raise ComparisonError("Re200 production solve is active; refusing to start a rank comparison")
    if request.output_dir.exists():
        raise ComparisonError(f"refusing to overwrite existing output directory: {request.output_dir}")
    request.output_dir.parent.mkdir(parents=True, exist_ok=True)
    print("RUN", shlex.join(request.command), flush=True)
    started = time.perf_counter()
    launch = subprocess.run(request.command, cwd=REPOSITORY_ROOT, check=False)
    wall_time = time.perf_counter() - started
    validator_command = (sys.executable, str(validator), str(request.output_dir))
    print("VALIDATE", shlex.join(validator_command), flush=True)
    validation = subprocess.run(validator_command, cwd=REPOSITORY_ROOT, check=False)
    row: dict[str, object] = {
        "case_id": request.case_id,
        "ranks": request.ranks,
        "command": shlex.join(request.command),
        "wall_time_seconds": wall_time,
        "launch_returncode": launch.returncode,
        "validator_command": shlex.join(validator_command),
        "validator_returncode": validation.returncode,
        "validation_passed": validation.returncode == 0,
    }
    try:
        row.update(parse_summary(request.output_dir))
    except (ComparisonError, FileNotFoundError, KeyError, ValueError, json.JSONDecodeError) as error:
        row["summary_error"] = str(error)
        print(f"SUMMARY unavailable for {request.output_dir}: {error}", file=sys.stderr, flush=True)
    return row


def parse_arguments(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case", action="append", choices=DEFAULT_CASES,
                        help="case ID to compare (repeatable; defaults to both selected cases)")
    parser.add_argument("--ranks", nargs="+", type=int, default=[2, 8],
                        help="MPI rank counts (default: 2 8)")
    parser.add_argument("--solver", type=Path, default=REPOSITORY_ROOT / "solver" / "build" / "cfd_solver",
                        help="solver executable")
    parser.add_argument("--mpi-launcher", default=os.environ.get("MPIEXEC", "mpirun"),
                        help="MPI launcher executable (default: $MPIEXEC or mpirun)")
    parser.add_argument("--output-root", type=Path,
                        default=REPOSITORY_ROOT / "solver" / "results" / "rank_comparisons",
                        help="root directory for comparison outputs")
    parser.add_argument("--validator", type=Path,
                        default=REPOSITORY_ROOT / "cfd_solver_agentic_benchmark" / "examiner" / "validate_outputs.py",
                        help="output-contract validator")
    parser.add_argument("--dry-run", action="store_true",
                        help="print exact commands without creating outputs or starting MPI")
    parser.add_argument("--fail-fast", action="store_true", help="stop after the first failed launch or validation")
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    arguments = parse_arguments(argv)
    solver = arguments.solver.resolve()
    validator = arguments.validator.resolve()
    output_root = arguments.output_root.resolve()
    if not solver.is_file():
        raise ComparisonError(f"solver executable does not exist: {solver}")
    if not os.access(solver, os.X_OK):
        raise ComparisonError(f"solver is not executable: {solver}")
    if not validator.is_file():
        raise ComparisonError(f"validator does not exist: {validator}")
    if re200_job_active():
        raise ComparisonError("Re200 production solve is active; refusing to start rank comparisons")

    requested_cases = tuple(arguments.case or DEFAULT_CASES)
    requests = [
        make_request(case_id=case_id, ranks=ranks, solver=solver,
                     mpi_launcher=arguments.mpi_launcher, output_root=output_root)
        for case_id in requested_cases
        for ranks in arguments.ranks
    ]
    if arguments.dry_run:
        for request in requests:
            print(shlex.join(request.command))
        return 0

    rows: list[dict[str, object]] = []
    for request in requests:
        row = run_one(request, validator)
        rows.append(row)
        write_comparison_csv(output_root / "comparison.csv", rows)
        failed = row["launch_returncode"] != 0 or not row["validation_passed"]
        if failed and arguments.fail_fast:
            break
    return 0 if all(row["launch_returncode"] == 0 and row["validation_passed"] for row in rows) else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ComparisonError as error:
        print(f"rank comparison error: {error}", file=sys.stderr)
        raise SystemExit(2)
