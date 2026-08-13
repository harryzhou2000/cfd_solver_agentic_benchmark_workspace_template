"""Orchestrate figure generation across all benchmark case directories.

Reads results from a results directory, runs plot_results.py and plot_field.py
for each case, and writes a figure_manifest.csv mapping figures to source data.

Usage:
    python generate_all_figures.py \
        --results-dir solver/results \
        --figures-dir solver/report/figures
"""
from __future__ import annotations

import argparse
import csv
import json
import os
import subprocess
import sys

# Use the venv python at solver/.venv
_HERE = os.path.dirname(os.path.abspath(__file__))
_VENV_PY = os.path.join(_HERE, "..", ".venv", "bin", "python")
_VENV_PY = os.path.abspath(_VENV_PY)
if not os.path.isfile(_VENV_PY):
    _VENV_PY = sys.executable  # fallback to current interpreter

KNOWN_CASES = [
    "naca0012_m015_inviscid",
    "naca0012_m080_inviscid",
    "naca0012_m200_inviscid",
    "naca0012_m015_laminar_re5000",
    "naca0012_m080_laminar_re5000",
    "naca0012_m200_laminar_re5000",
    "cylinder_m010_laminar_re20",
    "cylinder_m010_laminar_re200",
]


def find_case_dirs(results_dir):
    """Find case directories inside results_dir.

    Looks for subdirectories that contain at least one of:
    residuals.csv, forces.csv, surface.csv, field_final.vtu
    """
    case_dirs = []
    if not os.path.isdir(results_dir):
        return case_dirs
    for entry in sorted(os.listdir(results_dir)):
        full = os.path.join(results_dir, entry)
        if not os.path.isdir(full):
            continue
        has_data = any(
            os.path.isfile(os.path.join(full, f))
            for f in ("residuals.csv", "forces.csv", "surface.csv", "field_final.vtu",
                       "metadata.json", "run_status.json")
        )
        if has_data:
            case_dirs.append(entry)
    return case_dirs


def run_script(script, args):
    """Run a Python script with the venv interpreter, return list of stdout lines."""
    cmd = [_VENV_PY, script] + args
    print(f"  $ {' '.join(cmd)}")
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
    except subprocess.TimeoutExpired:
        print(f"  [error] timeout running {script}", file=sys.stderr)
        return []
    if result.returncode != 0:
        print(f"  [warn] {script} exited {result.returncode}", file=sys.stderr)
        if result.stderr:
            for line in result.stderr.strip().splitlines()[-5:]:
                print(f"    stderr: {line}", file=sys.stderr)
    out = result.stdout.strip().splitlines() if result.stdout else []
    return out


def collect_figures(out_dir):
    """List all PNG files in a directory."""
    figs = []
    if not os.path.isdir(out_dir):
        return figs
    for f in sorted(os.listdir(out_dir)):
        if f.lower().endswith(".png"):
            figs.append(os.path.join(out_dir, f))
    return figs


def infer_variable(fig_name):
    """Infer the plotted variable from a figure filename."""
    base = fig_name.replace(".png", "").lower()
    # e.g. mach_full, pressure_zoom, residuals_history
    if base.startswith("mach"):
        return "mach"
    if base.startswith("pressure"):
        return "pressure"
    if base.startswith("density"):
        return "density"
    if base.startswith("velocity"):
        return "velocity"
    if base.startswith("vorticity"):
        return "vorticity"
    if "temperature" in base:
        return "temperature"
    if base.startswith("residual"):
        return "residual"
    if base.startswith("cfl"):
        return "cfl"
    if "cp" in base:
        return "cp"
    if "cf" in base:
        return "cf"
    if "cl" in base or "cd" in base or "cmz" in base or "force" in base:
        return "forces"
    return "other"


def infer_plot_type(fig_name):
    """Infer the plot category."""
    base = fig_name.replace(".png", "").lower()
    if base.startswith(("mach", "pressure", "density", "velocity", "vorticity", "temperature")):
        return "field"
    if base.startswith("residual"):
        return "residual"
    if base.startswith("cfl"):
        return "cfl"
    if "surface" in base or "cp" in base or "cf" in base:
        return "surface"
    if "force" in base or base.startswith(("cl", "cd", "cmz")):
        return "force"
    return "other"


def main():
    ap = argparse.ArgumentParser(description="Generate all report figures")
    ap.add_argument("--results-dir", required=True, help="Directory containing case subdirs")
    ap.add_argument("--figures-dir", required=True, help="Output directory for figures")
    ap.add_argument("--cases", nargs="*", default=None,
                    help="Specific case names (default: auto-detect all)")
    ap.add_argument("--skip-field", action="store_true", help="Skip field (VTU) plotting")
    ap.add_argument("--skip-results", action="store_true", help="Skip results (CSV) plotting")
    args = ap.parse_args()

    results_dir = os.path.abspath(args.results_dir)
    figures_dir = os.path.abspath(args.figures_dir)
    os.makedirs(figures_dir, exist_ok=True)

    tools_dir = _HERE
    plot_results_script = os.path.join(tools_dir, "plot_results.py")
    plot_field_script = os.path.join(tools_dir, "plot_field.py")

    # Determine case list
    if args.cases:
        case_names = args.cases
    else:
        case_names = find_case_dirs(results_dir)

    if not case_names:
        print(f"No case directories found in {results_dir}")
        return 1

    print(f"Found {len(case_names)} cases: {', '.join(case_names)}")
    print(f"Results dir:  {results_dir}")
    print(f"Figures dir:  {figures_dir}")
    print(f"Python:       {_VENV_PY}")
    print()

    manifest_rows = []

    for case_name in case_names:
        case_dir = os.path.join(results_dir, case_name)
        case_out = os.path.join(figures_dir, case_name)
        os.makedirs(case_out, exist_ok=True)

        # Get case_id from metadata if available
        case_id = case_name
        meta_path = os.path.join(case_dir, "metadata.json")
        if os.path.isfile(meta_path):
            try:
                with open(meta_path) as fh:
                    meta = json.load(fh)
                cid = meta.get("case_id")
                if cid:
                    case_id = cid
            except Exception:
                pass

        print(f"{'='*60}")
        print(f"Case: {case_name}  (case_id={case_id})")
        print(f"{'='*60}")

        existing_figs_before = set(collect_figures(case_out))

        # 1. Results (residuals, forces, surface Cp)
        if not args.skip_results:
            print("\n--- Results plots ---")
            run_script(plot_results_script,
                       ["--case-dir", case_dir, "--output-dir", case_out])

        # 2. Field (VTU)
        if not args.skip_field:
            vtu_path = os.path.join(case_dir, "field_final.vtu")
            if os.path.isfile(vtu_path):
                print("\n--- Field plots ---")
                run_script(plot_field_script,
                           ["--vtu", vtu_path, "--output-dir", case_out,
                            "--case-id", case_id])
            else:
                print(f"\n  [skip] no field_final.vtu in {case_dir}")

        # Collect all generated figures for manifest
        all_figs = collect_figures(case_out)
        for fig_path in all_figs:
            fig_name = os.path.basename(fig_path)
            manifest_rows.append({
                "case_name": case_name,
                "case_id": case_id,
                "figure_file": os.path.relpath(fig_path, figures_dir),
                "variable": infer_variable(fig_name),
                "plot_type": infer_plot_type(fig_name),
                "source_dir": os.path.relpath(case_dir, results_dir),
            })

    # Write manifest
    manifest_path = os.path.join(figures_dir, "figure_manifest.csv")
    with open(manifest_path, "w", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=[
            "case_name", "case_id", "figure_file", "variable", "plot_type", "source_dir"
        ])
        writer.writeheader()
        writer.writerows(manifest_rows)

    print(f"\n{'='*60}")
    print(f"SUMMARY")
    print(f"{'='*60}")
    print(f"  Cases processed: {len(case_names)}")
    print(f"  Total figures:    {len(manifest_rows)}")
    print(f"  Manifest:         {manifest_path}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
