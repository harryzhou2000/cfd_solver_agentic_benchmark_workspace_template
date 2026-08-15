#!/usr/bin/env python3
"""Run benchmark cases reproducibly without embedding solver-specific logic."""
from __future__ import annotations

import argparse
import csv
import json
import shlex
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_CASES = ROOT / "cfd_solver_agentic_benchmark" / "inputs" / "cases"


def parse_case_specs(values: list[str], case_dir: Path) -> list[Path]:
    if not values:
        return sorted(case_dir.glob("*.json"))
    answer: list[Path] = []
    for value in values:
        path = Path(value)
        if not path.exists():
            path = case_dir / (value if value.endswith(".json") else value + ".json")
        if not path.exists():
            raise FileNotFoundError(f"case file not found: {value}")
        answer.append(path.resolve())
    return answer


def case_id(path: Path) -> str:
    with path.open() as handle:
        return str(json.load(handle).get("case_id", path.stem))


def write_manifest(path: Path, rows: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fields = ["case_id", "mpi_ranks", "case_file", "output_dir", "command", "return_code", "return_code_source", "wall_time_seconds", "started_utc", "convergence_status"]
    with path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cases", nargs="*", help="case ids or JSON paths (all supplied cases by default)")
    parser.add_argument("--solver", required=True, help="solver executable")
    parser.add_argument("--results", type=Path, default=ROOT / "solver" / "results")
    parser.add_argument("--case-dir", type=Path, default=DEFAULT_CASES)
    parser.add_argument("--mpi", type=int, default=1)
    parser.add_argument("--mpi-launcher", default="mpirun")
    parser.add_argument("--command-template", default="{mpi_launcher} -np {mpi} {solver} solve --case {case} --output {output}", help="format fields: mpi_launcher, mpi, solver, case, output")
    parser.add_argument("--extra-arg", action="append", default=[], help="extra token passed to solver")
    parser.add_argument("--continue-on-error", action="store_true")
    parser.add_argument("--manifest", type=Path, default=ROOT / "solver" / "report" / "run_manifest.csv")
    args = parser.parse_args()
    if args.mpi < 1:
        parser.error("--mpi must be positive")
    solver = Path(args.solver)
    if not solver.exists():
        parser.error(f"solver does not exist: {solver}")
    rows: list[dict[str, object]] = []
    result = 0
    for case in parse_case_specs(args.cases, args.case_dir):
        cid = case_id(case)
        output = (args.results / cid).resolve()
        output.mkdir(parents=True, exist_ok=True)
        command = args.command_template.format(mpi_launcher=args.mpi_launcher, mpi=args.mpi, solver=shlex.quote(str(solver.resolve())), case=shlex.quote(str(case)), output=shlex.quote(str(output)))
        command += " " + " ".join(shlex.quote(x) for x in args.extra_arg) if args.extra_arg else ""
        started = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
        start = time.monotonic()
        completed = subprocess.run(command, shell=True, cwd=ROOT, text=True)
        terminal_status = ""
        status_file = output / "run_status.json"
        if status_file.exists():
            try:
                terminal_status = str(json.loads(status_file.read_text()).get("convergence_status", ""))
            except (OSError, ValueError, TypeError):
                # Keep the launcher result even if a failing solver left an
                # incomplete status file; the blank field is honest evidence.
                pass
        # This is the only component that observes the launcher's process
        # status.  Keep that provenance explicit: a later report generated
        # from a directory produced by a direct command must not infer a zero
        # exit code merely because the package happens to contain outputs.
        row = {"case_id": cid, "mpi_ranks": args.mpi, "case_file": str(case), "output_dir": str(output), "command": command, "return_code": completed.returncode, "return_code_source": "run_cases launcher", "wall_time_seconds": f"{time.monotonic()-start:.6f}", "started_utc": started, "convergence_status": terminal_status}
        rows.append(row)
        write_manifest(args.manifest, rows)
        if completed.returncode:
            result = completed.returncode
            print(f"FAILED {cid}: exit {completed.returncode}", file=sys.stderr)
            if not args.continue_on_error:
                break
    return result


if __name__ == "__main__":
    raise SystemExit(main())
