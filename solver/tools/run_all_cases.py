#!/usr/bin/env python3
"""Run benchmark cases with the fv2d solver and record a run manifest.

Usage: python tools/run_all_cases.py [--np 8] [--cases case1,case2] [--tag NAME]
"""
import argparse
import json
import os
import subprocess
import sys
import time
from pathlib import Path

SOLVER_ROOT = Path(__file__).resolve().parent.parent
BENCH = SOLVER_ROOT.parent / "cfd_solver_agentic_benchmark"
CASES_DIR = BENCH / "inputs" / "cases"
RESULTS = SOLVER_ROOT / "results"
FV2D = SOLVER_ROOT / "build" / "fv2d"

ALL_CASES = [
    "naca0012_m015_inviscid",
    "naca0012_m080_inviscid",
    "naca0012_m200_inviscid",
    "naca0012_m015_laminar_re5000",
    "naca0012_m080_laminar_re5000",
    "naca0012_m200_laminar_re5000",
    "cylinder_m010_laminar_re20",
    "cylinder_m010_laminar_re200",
]


def run_case(case_id, np_, tag=None, extra_env=None):
    out = RESULTS / (case_id if tag is None else f"{case_id}_np{np_}" if tag == "np" else f"{case_id}_{tag}")
    out.mkdir(parents=True, exist_ok=True)
    cmd = ["mpirun", "-np", str(np_), str(FV2D), "solve", "--case",
           str(CASES_DIR / f"{case_id}.json"), "--output", str(out)]
    env = dict(os.environ)
    if extra_env:
        env.update(extra_env)
    t0 = time.time()
    with open(out / "runner.log", "w") as logf:
        proc = subprocess.run(cmd, stdout=logf, stderr=subprocess.STDOUT, env=env)
    wall = time.time() - t0
    status = "unknown"
    conv = ""
    try:
        st = json.load(open(out / "run_status.json"))
        status = st["convergence_status"]
        conv = st.get("residual_reduction_orders", "")
    except Exception:
        status = "failed_no_status"
    return {
        "case_id": case_id,
        "output_dir": str(out),
        "command": " ".join(cmd),
        "mpi_ranks": np_,
        "wall_time_seconds": round(wall, 1),
        "convergence_status": status,
        "residual_reduction_orders": conv,
        "exit_code": proc.returncode,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--np", type=int, default=8)
    ap.add_argument("--cases", type=str, default=",".join(ALL_CASES))
    ap.add_argument("--tag", type=str, default=None)
    ap.add_argument("--manifest", type=str, default=str(SOLVER_ROOT / "report" / "run_manifest.csv"))
    args = ap.parse_args()
    rows = []
    for cid in args.cases.split(","):
        print(f"[run_all] {cid} np={args.np}", flush=True)
        row = run_case(cid, args.np, args.tag)
        print("[run_all]   ->", row["convergence_status"], row["wall_time_seconds"], "s", flush=True)
        rows.append(row)
    # append to manifest
    import csv
    mpath = Path(args.manifest)
    mpath.parent.mkdir(parents=True, exist_ok=True)
    write_header = not mpath.exists()
    with open(mpath, "a", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["case_id", "command", "mpi_ranks", "wall_time_seconds",
                                          "convergence_status", "residual_reduction_orders", "exit_code",
                                          "output_dir"])
        if write_header:
            w.writeheader()
        for r in rows:
            w.writerow(r)
    print(f"[run_all] manifest updated: {mpath}")


if __name__ == "__main__":
    main()
