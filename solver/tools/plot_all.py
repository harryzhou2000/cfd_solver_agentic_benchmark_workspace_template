#!/usr/bin/env python3
"""Master plotting script: generate every figure for every benchmark case.

Usage:
    python plot_all.py [results_dir] [figures_dir]

For each case directory under results_dir the four plot scripts are invoked
as subprocesses:
    plot_residuals, plot_forces, plot_surface, plot_field

Output figures land in <figures_dir> (default solver/report/figures).
"""

import os
import subprocess
import sys

CASES = [
    "naca0012_m015_inviscid", "naca0012_m080_inviscid", "naca0012_m200_inviscid",
    "naca0012_m015_laminar_re5000", "naca0012_m080_laminar_re5000",
    "naca0012_m200_laminar_re5000",
    "cylinder_m010_laminar_re20", "cylinder_m010_laminar_re200",
]

SCRIPTS = ["plot_residuals", "plot_forces", "plot_surface", "plot_field"]


def main():
    tools_dir = os.path.dirname(os.path.abspath(__file__))
    solver_dir = os.path.dirname(tools_dir)

    results_dir = (sys.argv[1] if len(sys.argv) > 1
                   else os.path.join(solver_dir, "results"))
    figures_dir = (sys.argv[2] if len(sys.argv) > 2
                   else os.path.join(solver_dir, "report", "figures"))
    os.makedirs(figures_dir, exist_ok=True)

    n_cases = 0
    n_figures = 0
    failed = []
    skipped = []

    for case in CASES:
        case_dir = os.path.join(results_dir, case)
        if not os.path.isdir(case_dir):
            print(f"SKIP {case}: no results directory")
            continue
        n_cases += 1
        print(f"=== {case} ===")
        for script in SCRIPTS:
            proc = subprocess.run(
                [sys.executable, os.path.join(tools_dir, f"{script}.py"),
                 case_dir, case, figures_dir],
                cwd=tools_dir, check=False)
            if proc.returncode == 2:
                skipped.append((case, script))
            elif proc.returncode != 0:
                failed.append((case, script))

    print()
    print(f"Cases processed: {n_cases}/{len(CASES)}")
    for fn in sorted(os.listdir(figures_dir)):
        if fn.endswith(".png"):
            n_figures += 1
    print(f"Figures in {figures_dir}: {n_figures}")
    if skipped:
        print(f"Skipped (missing/incomplete data): {len(skipped)}")
        for case, script in skipped:
            print(f"  {case}: {script}")
    if failed:
        print(f"FAILED scripts ({len(failed)}):")
        for case, script in failed:
            print(f"  {case}: {script}")
        sys.exit(1)
    print("All plots completed.")


if __name__ == "__main__":
    main()
