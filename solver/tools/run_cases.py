#!/usr/bin/env python3
"""Run the required production cases and record the exact reproducibility manifest."""

from __future__ import annotations

import argparse
import asyncio
import csv
import json
import os
from pathlib import Path
import shlex
import shutil
import time


REQUIRED_CASES = (
    "naca0012_m015_inviscid",
    "naca0012_m080_inviscid",
    "naca0012_m200_inviscid",
    "naca0012_m015_laminar_re5000",
    "naca0012_m080_laminar_re5000",
    "naca0012_m200_laminar_re5000",
    "cylinder_m010_laminar_re20",
    "cylinder_m010_laminar_re200",
)

COMPARISON_CASES = (
    "naca0012_m015_inviscid",
    "cylinder_m010_laminar_re20",
)


def shell_join(parts: list[str]) -> str:
    return " ".join(shlex.quote(part) for part in parts)


def complete_output(path: Path) -> bool:
    try:
        status = json.loads((path / "run_status.json").read_text())
        metadata = json.loads((path / "metadata.json").read_text())
    except (FileNotFoundError, json.JSONDecodeError):
        return False
    return status.get("convergence_status") in {"converged", "statistically_periodic"} and metadata.get("completed") is True


async def run_one(case_id: str, ranks: int, output_dir: Path, executable: Path,
                  case_root: Path, results_root: Path, force: bool,
                  semaphore: asyncio.Semaphore) -> dict[str, object]:
    case_file = case_root / f"{case_id}.json"
    command = [
        "mpirun", "--bind-to", "core", "--map-by", "slot", "-np", str(ranks),
        str(executable), "solve", "--case", str(case_file), "--output", str(output_dir),
        "--report-level", "full",
    ]
    if complete_output(output_dir) and not force:
        status = json.loads((output_dir / "run_status.json").read_text())
        return {
            "case_id": case_id,
            "mpi_ranks": ranks,
            "command": shell_join(command),
            "wall_time_seconds": status.get("wall_time_seconds", 0.0),
            "convergence_status": status.get("convergence_status", "unknown"),
            "output_directory": str(output_dir),
            "driver_status": "reused",
        }
    if output_dir.exists():
        if any(output_dir.iterdir()):
            if not force:
                raise RuntimeError(f"incomplete output exists (use --force): {output_dir}")
            resolved_output = output_dir.resolve()
            resolved_results = results_root.resolve()
            if resolved_results not in resolved_output.parents:
                raise RuntimeError(
                    f"refusing to replace output outside {resolved_results}: {resolved_output}"
                )
            shutil.rmtree(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    # Keep the orchestration log outside the solver-owned case directory.  The
    # executable deliberately refuses nonempty output directories so stale
    # fields can never be mixed with a new state.
    driver_log = output_dir.parent / "_driver_logs" / f"{output_dir.name}_np{ranks}.log"
    driver_log.parent.mkdir(parents=True, exist_ok=True)
    async with semaphore:
        start = time.monotonic()
        print(f"START {case_id} np={ranks}: {shell_join(command)}", flush=True)
        with driver_log.open("wb") as log:
            process = await asyncio.create_subprocess_exec(
                *command, stdout=log, stderr=asyncio.subprocess.STDOUT,
                env={**os.environ, "OMP_NUM_THREADS": "1"},
            )
            return_code = await process.wait()
    elapsed = time.monotonic() - start
    if return_code != 0:
        raise RuntimeError(f"{case_id} np={ranks} failed with exit {return_code}; see {driver_log}")
    if not complete_output(output_dir):
        raise RuntimeError(f"{case_id} exited zero without a completed final output")
    status = json.loads((output_dir / "run_status.json").read_text())
    print(f"DONE  {case_id} np={ranks} in {elapsed:.1f}s ({status['convergence_status']})", flush=True)
    return {
        "case_id": case_id,
        "mpi_ranks": ranks,
        "command": shell_join(command),
        "wall_time_seconds": status.get("wall_time_seconds", elapsed),
        "convergence_status": status["convergence_status"],
        "output_directory": str(output_dir),
        "driver_status": "executed",
    }


async def run_all(args: argparse.Namespace) -> list[dict[str, object]]:
    semaphore = asyncio.Semaphore(args.concurrent)
    executable = args.executable.resolve()
    case_root = args.case_root.resolve()
    tasks = [
        asyncio.create_task(
            run_one(case_id, args.ranks, args.results / case_id, executable,
                    case_root, args.results, args.force, semaphore)
        )
        for case_id in args.cases
    ]
    records = list(await asyncio.gather(*tasks))
    if args.rank_comparisons:
        comparison_tasks = []
        for case_id in COMPARISON_CASES:
            if case_id not in args.cases:
                continue
            for ranks in args.comparison_ranks:
                if ranks == args.ranks:
                    continue
                output = args.results / "rank_comparisons" / f"{case_id}_np{ranks}"
                comparison_tasks.append(
                    asyncio.create_task(
                        run_one(case_id, ranks, output, executable, case_root,
                                args.results, args.force, semaphore)
                    )
                )
        records.extend(await asyncio.gather(*comparison_tasks))
    return records


def write_manifest(path: Path, records: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fields = ("case_id", "mpi_ranks", "command", "wall_time_seconds",
              "convergence_status", "output_directory", "driver_status")
    with path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(records)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, default=Path("build/aerofv"))
    parser.add_argument("--case-root", type=Path, default=Path("inputs/cases"))
    parser.add_argument("--results", type=Path, default=Path("results"))
    parser.add_argument("--manifest", type=Path, default=Path("report/run_manifest.csv"))
    parser.add_argument("--ranks", type=int, default=8)
    parser.add_argument("--concurrent", type=int, default=2)
    parser.add_argument("--cases", nargs="+", choices=REQUIRED_CASES, default=list(REQUIRED_CASES))
    parser.add_argument("--rank-comparisons", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--comparison-ranks", type=int, nargs="+", default=[1, 2, 4])
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()
    if args.ranks < 1 or args.concurrent < 1 or any(value < 1 for value in args.comparison_ranks):
        parser.error("rank counts and concurrency must be positive")
    if not args.executable.exists():
        parser.error(f"solver executable does not exist: {args.executable}")
    records = asyncio.run(run_all(args))
    write_manifest(args.manifest, records)
    print(f"wrote {args.manifest}")


if __name__ == "__main__":
    main()
