#!/usr/bin/env python3
"""Run CFD solver cases with documented, reproducible commands.

Each case runs the production solver binary (solver/build/cfd_solver) from the
workspace root with the exact MPI command recorded in run_manifest.csv:

    mpirun -np <ranks> solver/build/cfd_solver solve --case <case-json> \
        --output <output-dir> [--max-steps <N>]

Usage:
    python3 tools/run_cases.py                    # run every case below
    python3 tools/run_cases.py --case naca0012_m015_inviscid
    python3 tools/run_cases.py --list
"""

import argparse
import os
import subprocess
import sys

WORKSPACE_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))
CASE_DIR = os.path.join(WORKSPACE_ROOT, "cfd_solver_agentic_benchmark",
                        "inputs", "cases")
RESULTS_DIR = os.path.join(WORKSPACE_ROOT, "solver", "results")
SOLVER = os.path.join(WORKSPACE_ROOT, "solver", "build", "cfd_solver")

# (case json, output dir, mpi ranks, optional max_steps override)
# The short transient run (re200) documents that the production horizon
# t=300.0 / 30000 steps was not reached; see report Limitations.
CASES = [
    ("naca0012_m015_inviscid.json", "naca0012_m015_inviscid", 1, None),
    ("naca0012_m015_inviscid.json", "naca0012_m015_inviscid_np2", 2, None),
    ("naca0012_m015_inviscid.json", "naca0012_m015_inviscid_np4", 4, None),
    ("naca0012_m015_inviscid.json", "naca0012_m015_inviscid_np8", 8, None),
    ("cylinder_m010_laminar_re20.json", "cylinder_m010_laminar_re20", 1, None),
    ("cylinder_m010_laminar_re200.json", "cylinder_m010_laminar_re200", 2,
     100),
]


def run_case(case_json, output_dir, np=1, max_steps=None, env=None):
    cmd = ["mpirun", "-np", str(np), SOLVER, "solve",
           "--case", case_json, "--output", output_dir]
    if max_steps:
        cmd.extend(["--max-steps", str(max_steps)])
    print("+ " + " ".join(cmd))
    subprocess.run(cmd, check=True, env=env)
    return cmd


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case", default=None,
                        help="run only this output-dir name (see --list)")
    parser.add_argument("--list", action="store_true",
                        help="list the configured cases and exit")
    args = parser.parse_args()

    if args.list:
        for name, out, np_, ms in CASES:
            print(f"  {out:32s} np={np_} max_steps={ms}")
        return 0

    # The solver needs the external shared libraries at runtime.
    env = dict(os.environ)
    ext_lib = os.path.join(WORKSPACE_ROOT, "external", "cfd_externals",
                           "install", "lib")
    env["LD_LIBRARY_PATH"] = ext_lib + (
        ":" + env["LD_LIBRARY_PATH"] if env.get("LD_LIBRARY_PATH") else "")

    os.makedirs(RESULTS_DIR, exist_ok=True)
    for case_json, out_name, np_, ms in CASES:
        if args.case and args.case != out_name:
            continue
        run_case(os.path.join(CASE_DIR, case_json),
                 os.path.join(RESULTS_DIR, out_name), np_, ms, env=env)
    return 0


if __name__ == "__main__":
    sys.exit(main())
