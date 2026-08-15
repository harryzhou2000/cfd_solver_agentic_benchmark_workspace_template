#!/usr/bin/env python3
"""Generate ALL report figures for the CFD solver benchmark (Phase 6).

For every completed case directory this script produces:

  <case>_residual_history.png   L2 residual and conservative-variable
                                component norms vs step (log scale)
  <case>_force_history.png      CL and CD vs step
  <case>_surface_cp.png         NACA0012: Cp vs x/c, upper and lower surface
  <case>_surface_wall.png       cylinder: Cp vs angle around the wall
  <case>_mach.png               Mach number contours (near-body/wake zoom)
  <case>_pressure.png           pressure contours (near-body/wake zoom)
  <case>_velocity_mag.png       cylinder Re20: velocity magnitude, wake zoom
  <case>_vorticity.png          cylinder Re200: vorticity clipped to [-5, 5]

Field contours reuse solver/tools/plot_fields.py. All figures are written to
solver/report/figures/ (dpi=200, 14 pt fonts). With --write-manifest the
figure manifest (figure_manifest.csv) is regenerated from the figures that
exist on disk, so the manifest and the disk always agree.

Usage:
    python3 plot_results.py [RESULT_DIR...] [--figdir FIGDIR]
                            [--write-manifest]
"""

from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402

from plot_fields import make_field_figures  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
DEFAULT_RESULTS = REPO_ROOT / "solver" / "results"
DEFAULT_FIGDIR = REPO_ROOT / "solver" / "report" / "figures"

DPI = 200
LW = 1.8

plt.rcParams.update(
    {
        "font.size": 14,
        "axes.labelsize": 15,
        "axes.titlesize": 15,
        "xtick.labelsize": 12,
        "ytick.labelsize": 12,
        "legend.fontsize": 12,
        "lines.linewidth": LW,
        "axes.grid": True,
        "grid.alpha": 0.3,
        "grid.linestyle": "-",
        "figure.dpi": DPI,
        "savefig.dpi": DPI,
        "savefig.bbox": "tight",
    }
)

RESIDUAL_COLORS = {
    "rho": "#1f77b4",
    "rhou": "#ff7f0e",
    "rhov": "#2ca02c",
    "rhoE": "#d62728",
    "residual_l2": "#000000",
}


def read_csv(path: Path) -> list[dict[str, str]] | None:
    if not path.exists():
        return None
    with path.open(newline="") as f:
        return list(csv.DictReader(f))


def positive(values) -> np.ndarray:
    """Mask non-positive values so semilogy stays finite."""
    return np.where(np.asarray(values) > 0.0, values, np.nan)


def plot_residual_history(res_dir: Path, figdir: Path) -> Path | None:
    rows = read_csv(res_dir / "residuals.csv")
    if not rows:
        print(f"SKIP {res_dir.name}: no residuals.csv")
        return None
    step = np.array([float(r["step"]) for r in rows])
    out = figdir / f"{res_dir.name}_residual_history.png"
    fig, ax = plt.subplots(figsize=(9, 5.5))
    for key, color in RESIDUAL_COLORS.items():
        vals = positive([float(r[key]) for r in rows])
        ax.semilogy(step, vals, color=color, label=key, lw=LW)
    ax.set_xlabel("Iteration step")
    ax.set_ylabel("Residual norm (L2)")
    ax.set_title(f"{res_dir.name} — residual history")
    ax.legend(ncol=2, framealpha=0.9)
    fig.tight_layout()
    fig.savefig(out)
    plt.close(fig)
    print(f"OK {out}")
    return out


def plot_force_history(res_dir: Path, figdir: Path) -> Path | None:
    rows = read_csv(res_dir / "forces.csv")
    if not rows:
        print(f"SKIP {res_dir.name}: no forces.csv")
        return None
    step = np.array([float(r["step"]) for r in rows])
    cl = np.array([float(r["cl"]) for r in rows])
    cd = np.array([float(r["cd"]) for r in rows])
    out = figdir / f"{res_dir.name}_force_history.png"
    fig, (ax1, ax2) = plt.subplots(
        2, 1, figsize=(9, 6.5), sharex=True)
    ax1.plot(step, cl, color="#1f77b4", lw=LW)
    ax1.set_ylabel(r"$C_L$")
    ax1.set_title(f"{res_dir.name} — force history")
    ax2.plot(step, cd, color="#d62728", lw=LW)
    ax2.set_ylabel(r"$C_D$")
    ax2.set_xlabel("Iteration step")
    fig.tight_layout()
    fig.savefig(out)
    plt.close(fig)
    print(f"OK {out}")
    return out


def plot_surface_cp(res_dir: Path, figdir: Path) -> Path | None:
    """NACA airfoil: Cp vs x/c along the upper and lower surfaces."""
    rows = read_csv(res_dir / "surface.csv")
    if not rows or "naca" not in res_dir.name:
        return None
    upper = [(float(r["x"]), float(r["cp"])) for r in rows
             if float(r["y"]) > 1e-6]
    lower = [(float(r["x"]), float(r["cp"])) for r in rows
             if float(r["y"]) < -1e-6]
    if not upper or not lower:
        print(f"SKIP {res_dir.name}: cannot split upper/lower surface rows")
        return None
    out = figdir / f"{res_dir.name}_surface_cp.png"
    fig, ax = plt.subplots(figsize=(8, 5.5))
    ax.plot(*zip(*sorted(upper)), label="upper surface", color="#1f77b4",
            lw=LW)
    ax.plot(*zip(*sorted(lower)), label="lower surface", color="#d62728",
            lw=LW)
    ax.invert_yaxis()  # suction peaks point upward (aero convention)
    ax.set_xlabel(r"$x/c$")
    ax.set_ylabel(r"$C_p$")
    ax.set_title(f"{res_dir.name} — surface pressure coefficient")
    ax.legend(framealpha=0.9)
    fig.tight_layout()
    fig.savefig(out)
    plt.close(fig)
    print(f"OK {out}")
    return out


def plot_surface_wall(res_dir: Path, figdir: Path) -> Path | None:
    """Cylinder: Cp vs angle around the wall, 0 deg at the front stagnation."""
    rows = read_csv(res_dir / "surface.csv")
    if not rows or "cylinder" not in res_dir.name:
        return None
    theta = []
    cp = []
    for r in rows:
        ang = np.degrees(np.arctan2(float(r["y"]), float(r["x"])))
        theta.append(180.0 - ang)  # 0 deg at the front stagnation point
        cp.append(float(r["cp"]))
    theta = np.asarray(theta) % 360.0
    order = np.argsort(theta)
    out = figdir / f"{res_dir.name}_surface_wall.png"
    fig, ax = plt.subplots(figsize=(8, 5.5))
    ax.plot(theta[order], np.asarray(cp)[order], color="#2ca02c", lw=LW)
    ax.set_xlabel(r"angle around cylinder $\theta$ (deg)")
    ax.set_ylabel(r"$C_p$")
    ax.set_title(f"{res_dir.name} — wall pressure coefficient")
    ax.set_xlim(0, 360)
    fig.tight_layout()
    fig.savefig(out)
    plt.close(fig)
    print(f"OK {out}")
    return out


def write_figure_manifest(figdir: Path) -> None:
    """Regenerate figure_manifest.csv from the figures on disk.

    One row per figure with figure_file, case_id, figure_type, variable,
    source_file, caption. The source file is the solver CSV/VTK file the
    figure was produced from; the variable column always matches the
    variable named by the file name (mach/pressure/velocity/vorticity).
    """
    captions = {
        "_residual_history.png": (
            "L2 residual norm and conservative-variable component norms vs "
            "iteration step (log scale), from residuals.csv"),
        "_force_history.png": (
            "Lift and drag coefficient histories vs iteration step, from "
            "forces.csv"),
        "_surface_cp.png": (
            "Surface pressure coefficient Cp vs x/c on the upper and lower "
            "airfoil surfaces, from surface.csv"),
        "_surface_wall.png": (
            "Wall pressure coefficient Cp vs angle around the cylinder, from "
            "surface.csv"),
        "_mach.png": ("Mach number contours from the converged field "
                      "(field_final.vtk)"),
        "_pressure.png": ("Pressure contours from the converged field "
                          "(field_final.vtk); range clipped to the 0.5-99.5 "
                          "percentiles"),
        "_velocity_mag.png": ("Velocity magnitude contours from the "
                              "converged field (field_final.vtk)"),
        "_vorticity.png": ("Vorticity (z) contours clipped to [-5, 5] from "
                           "the converged field (field_final.vtk)"),
    }
    variables = {
        "_residual_history.png": "residual",
        "_force_history.png": "force_coefficients",
        "_surface_cp.png": "cp",
        "_surface_wall.png": "cp",
        "_mach.png": "mach",
        "_pressure.png": "pressure",
        "_velocity_mag.png": "velocity",
        "_vorticity.png": "vorticity",
    }
    sources = {
        "_residual_history.png": "residuals.csv",
        "_force_history.png": "forces.csv",
        "_surface_cp.png": "surface.csv",
        "_surface_wall.png": "surface.csv",
        "_mach.png": "field_final.vtk",
        "_pressure.png": "field_final.vtk",
        "_velocity_mag.png": "field_final.vtk",
        "_vorticity.png": "field_final.vtk",
    }
    entries = []
    for fig in sorted(figdir.glob("*.png")):
        suffix = None
        for key in captions:
            if fig.name.endswith(key):
                suffix = key
                break
        if suffix is None:
            print(f"WARN {fig.name}: unrecognized figure type, skipping")
            continue
        case_id = fig.name[: -len(suffix)]
        entries.append({
            "figure_file": fig.name,
            "case_id": case_id,
            "figure_type": suffix[1:-4],  # e.g. residual_history
            "variable": variables[suffix],
            "source_file": sources[suffix],
            "caption": f"{case_id}: {captions[suffix]}",
        })
    out = figdir.parent / "figure_manifest.csv"
    with out.open("w", newline="") as f:
        writer = csv.DictWriter(
            f, fieldnames=["figure_file", "case_id", "figure_type",
                           "variable", "source_file", "caption"])
        writer.writeheader()
        writer.writerows(entries)
    print(f"wrote {out} ({len(entries)} entries)")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("result_dirs", nargs="*", type=Path,
                        help="case result directories (default: all "
                             "subdirectories of solver/results with "
                             "run_status.json)")
    parser.add_argument("--figdir", type=Path, default=DEFAULT_FIGDIR)
    parser.add_argument("--write-manifest", action="store_true",
                        help="regenerate figure_manifest.csv from disk")
    args = parser.parse_args()

    figdir = args.figdir.resolve()
    figdir.mkdir(parents=True, exist_ok=True)

    dirs: list[Path] = []
    if args.result_dirs:
        dirs = [d.resolve() for d in args.result_dirs]
    else:
        for d in sorted(DEFAULT_RESULTS.iterdir()):
            if d.is_dir() and (d / "run_status.json").exists():
                dirs.append(d)

    for res in dirs:
        plot_residual_history(res, figdir)
        plot_force_history(res, figdir)
        plot_surface_cp(res, figdir)
        plot_surface_wall(res, figdir)
        make_field_figures(res, figdir)

    if args.write_manifest:
        write_figure_manifest(figdir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
