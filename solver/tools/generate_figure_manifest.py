#!/usr/bin/env python3
"""Generate report/figure_manifest.csv from the produced figure PNGs.

Columns: figure_file, case_id, figure_type, variable, source_file, caption

Usage:
    python generate_figure_manifest.py [figures_dir] [output_csv]
"""

import csv
import os
import sys

FIGURE_TYPES = ["residual", "forces", "cp", "mach", "pressure", "vorticity"]

VARIABLES = {
    "residual": "residual_l2, residual_linf, rho, rhou, rhov, rhoE",
    "forces": "cl, cd",
    "cp": "cp",
    "mach": "mach",
    "pressure": "pressure",
    "vorticity": "vorticity",
}

SOURCE_FILES = {
    "residual": "residuals.csv",
    "forces": "forces.csv",
    "cp": "surface.csv",
    "mach": "field_final.vtu",
    "pressure": "field_final.vtu",
    "vorticity": "field_final.vtu",
}

CAPTIONS = {
    "residual": "Convergence history: global and per-component residuals",
    "forces": "Aerodynamic force coefficient history (Cl, Cd)",
    "cp": "Surface pressure coefficient distribution",
    "mach": "Mach number contours of the final flow field",
    "pressure": "Static pressure contours of the final flow field",
    "vorticity": "Vorticity contours of the final flow field",
}


def parse_figure_filename(fn):
    """Split '<case_id>_<figure_type>.png' into (case_id, figure_type)."""
    stem = fn[:-4] if fn.endswith(".png") else fn
    for ftype in FIGURE_TYPES:
        suffix = "_" + ftype
        if stem.endswith(suffix):
            return stem[: -len(suffix)], ftype
    return None, None


def main():
    tools_dir = os.path.dirname(os.path.abspath(__file__))
    solver_dir = os.path.dirname(tools_dir)
    figures_dir = (sys.argv[1] if len(sys.argv) > 1
                   else os.path.join(solver_dir, "report", "figures"))
    out_csv = (sys.argv[2] if len(sys.argv) > 2
               else os.path.join(solver_dir, "report", "figure_manifest.csv"))

    if not os.path.isdir(figures_dir):
        print(f"no figures directory: {figures_dir}")
        sys.exit(1)

    os.makedirs(os.path.dirname(out_csv), exist_ok=True)
    rows = []
    for fn in sorted(os.listdir(figures_dir)):
        if not fn.endswith(".png"):
            continue
        case_id, ftype = parse_figure_filename(fn)
        if case_id is None or ftype is None:
            print(f"  [warn] unrecognized figure name: {fn}")
            continue
        rows.append([fn, case_id, ftype, VARIABLES[ftype],
                     SOURCE_FILES[ftype], CAPTIONS[ftype]])

    with open(out_csv, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["figure_file", "case_id", "figure_type", "variable",
                    "source_file", "caption"])
        w.writerows(rows)

    print(f"wrote {out_csv} with {len(rows)} entries")


if __name__ == "__main__":
    main()
