#!/usr/bin/env python3
"""Generate all report figures from solver outputs (pure stdlib rendering).

Usage: python3 scripts/make_figures.py <results_dir> <figures_dir>
"""

import csv
import json
import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools_py"))
from plot_lines import main as plot_lines_main
from plot_field import main as plot_field_main


CASES = [
    "naca0012_m015_inviscid",
    "naca0012_m080_inviscid",
    "naca0012_m200_inviscid",
    "naca0012_m015_laminar_re5000",
    "naca0012_m080_laminar_re5000",
    "naca0012_m200_laminar_re5000",
    "cylinder_m010_laminar_re20",
    "cylinder_m010_laminar_re200",
]

NACA = [c for c in CASES if c.startswith("naca")]
CYL = [c for c in CASES if c.startswith("cylinder")]


def plot_lines(out, csvpath, xcol, ycols, title, xlabel, ylabel, logy=True,
               legend=None):
    args = [out, csvpath, xcol, ",".join(ycols), "--title", title,
            "--xlabel", xlabel, "--ylabel", ylabel]
    if logy:
        args.append("--logy")
    if legend:
        args += ["--legend", ",".join(legend)]
    plot_lines_main(args)


def plot_field(out, vtu, scalar, title, label, xr, yr, pcts="1,99", cmap="coolwarm"):
    args = [out, vtu, scalar, "--xmin", str(xr[0]), "--xmax", str(xr[1]),
            "--ymin", str(yr[0]), "--ymax", str(yr[1]), "--title", title,
            "--label", label, "--percentiles", pcts, "--cmap", cmap]
    plot_field_main(args)


def main():
    results = sys.argv[1] if len(sys.argv) > 1 else "results"
    figs = sys.argv[2] if len(sys.argv) > 2 else "report/figures"
    os.makedirs(figs, exist_ok=True)

    for case in CASES:
        rdir = os.path.join(results, case)
        if not os.path.exists(os.path.join(rdir, "forces.csv")):
            print(f"skip {case}: no outputs")
            continue
        tag = case
        # residual history
        plot_lines(
            os.path.join(figs, f"{tag}_residual.png"),
            os.path.join(rdir, "residuals.csv"), "step", ["residual_l2"],
            f"{case}: residual history", "step", "residual L2",
            logy=True, legend=["L2"])
        # force history
        plot_lines(
            os.path.join(figs, f"{tag}_forces.png"),
            os.path.join(rdir, "forces.csv"), "step", ["cd", "cl"],
            f"{case}: force coefficients", "step", "coefficient",
            logy=False, legend=["CD", "CL"])
        # surface pressure
        if os.path.exists(os.path.join(rdir, "surface.csv")):
            plot_lines(
                os.path.join(figs, f"{tag}_surface_cp.png"),
                os.path.join(rdir, "surface.csv"), "x", ["cp"],
                f"{case}: wall pressure coefficient", "x", "Cp",
                logy=False, legend=["Cp"])
            if "laminar" in case:
                plot_lines(
                    os.path.join(figs, f"{tag}_surface_cf.png"),
                    os.path.join(rdir, "surface.csv"), "x", ["cf"],
                    f"{case}: wall skin friction", "x", "cf",
                    logy=False, legend=["cf"])

    # Field plots: near-body zoom for NACA, wake zoom for cylinder.
    for case in NACA:
        rdir = os.path.join(results, case)
        vtu = os.path.join(rdir, "field_final.vtu")
        if not os.path.exists(vtu):
            continue
        plot_field(os.path.join(figs, f"{case}_mach_near.png"), vtu, "mach",
                   f"{case}: Mach number (near body)", "Mach",
                   (-0.2, 1.3), (-0.7, 0.7), pcts="1,99", cmap="viridis")
        plot_field(os.path.join(figs, f"{case}_pressure_near.png"), vtu,
                   "pressure", f"{case}: pressure (near body)", "pressure",
                   (-0.2, 1.3), (-0.7, 0.7), pcts="1,99", cmap="coolwarm")
        plot_field(os.path.join(figs, f"{case}_mach_full.png"), vtu, "mach",
                   f"{case}: Mach number (full domain)", "Mach",
                   (-5, 6), (-5, 5), pcts="1,99", cmap="viridis")
        plot_field(os.path.join(figs, f"{case}_pressure_full.png"), vtu,
                   "pressure", f"{case}: pressure (full domain)", "pressure",
                   (-5, 6), (-5, 5), pcts="1,99", cmap="coolwarm")

    for case in CYL:
        rdir = os.path.join(results, case)
        vtu = os.path.join(rdir, "field_final.vtu")
        if not os.path.exists(vtu):
            continue
        plot_field(os.path.join(figs, f"{case}_mach_near.png"), vtu, "mach",
                   f"{case}: Mach number (near body)", "Mach",
                   (-1.2, 2.2), (-1.2, 1.2), pcts="1,99", cmap="viridis")
        plot_field(os.path.join(figs, f"{case}_pressure_near.png"), vtu,
                   "pressure", f"{case}: pressure (near body)", "pressure",
                   (-1.2, 2.2), (-1.2, 1.2), pcts="1,99", cmap="coolwarm")
        plot_field(os.path.join(figs, f"{case}_velocity_near.png"), vtu,
                   "velocity", f"{case}: velocity magnitude (near body)",
                   "|u|", (-1.2, 2.2), (-1.2, 1.2), pcts="1,99", cmap="viridis")
        # Wake zoom for every cylinder case (Re20 steady wake, Re200 vortex
        # street); the near-body view alone does not show the wake structure.
        plot_field(os.path.join(figs, f"{case}_velocity_wake.png"), vtu,
                   "velocity", f"{case}: velocity magnitude (wake)",
                   "|u|", (0.0, 8.0), (-3.0, 3.0), pcts="1,99",
                   cmap="viridis")
        plot_field(os.path.join(figs, f"{case}_pressure_wake.png"), vtu,
                   "pressure", f"{case}: pressure (wake)", "pressure",
                   (0.0, 8.0), (-3.0, 3.0), pcts="1,99", cmap="coolwarm")

    print("figures done")


if __name__ == "__main__":
    main()
