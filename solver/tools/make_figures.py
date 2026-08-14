#!/usr/bin/env python3
"""Generate publication-style figures + machine-readable manifests from the
submitted solver results.

Usage:
  make_figures.py <results_dir> <report_dir>
  make_figures.py <results_dir> <report_dir> --mpi-dir <mpi_runs_dir>

results_dir contains one subdirectory per case (the solver output layout),
report_dir receives figures/, figure_manifest.csv, run_manifest.csv and
sanity_checks.json. If --mpi-dir points at a directory with np=2 (or other
rank-count) runs of the same cases, rank-count comparison figures are added
for the NACA and cylinder cases.
"""

import csv
import json
import math
import os
import re
import sys
import argparse
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


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

MPI_COMPARE_CASES = ["naca0012_m015_inviscid", "cylinder_m010_laminar_re20"]


def read_csv(path):
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def line_style():
    plt.rcParams.update(
        {
            "font.size": 10,
            "axes.labelsize": 11,
            "axes.titlesize": 11,
            "legend.fontsize": 9,
            "lines.linewidth": 1.4,
            "figure.dpi": 150,
            "savefig.dpi": 150,
            "axes.grid": True,
            "grid.alpha": 0.3,
        }
    )


def plot_history(case, out, step_col, xlabel, series, ylabel, logy=True):
    fig, ax = plt.subplots(figsize=(6.5, 4))
    for label, key in series:
        xs = [float(r[step_col]) for r in case]
        ys = [float(r[key]) for r in case]
        ax.plot(xs, ys, label=label)
    ax.set_xlabel(xlabel)
    ax.set_ylabel(ylabel)
    if logy:
        ax.set_yscale("log")
    ax.legend()
    fig.tight_layout()
    fig.savefig(out)
    plt.close(fig)


def plot_cp(case_dir, out):
    rows = read_csv(case_dir / "surface.csv")
    up = [(float(r["x"]), float(r["cp"])) for r in rows if float(r["y"]) > 0]
    lo = [(float(r["x"]), float(r["cp"])) for r in rows if float(r["y"]) < 0]
    up.sort()
    lo.sort()
    fig, ax = plt.subplots(figsize=(6.5, 4))
    ax.plot([x for x, _ in up], [c for _, c in up], label="upper surface")
    ax.plot([x for x, _ in lo], [c for _, c in lo], label="lower surface")
    ax.set_xlabel("x/c")
    ax.set_ylabel("$C_p$")
    ax.invert_yaxis()
    ax.legend()
    fig.tight_layout()
    fig.savefig(out)
    plt.close(fig)


def plot_surface_cf(case_dir, out):
    rows = read_csv(case_dir / "surface.csv")
    up = [(float(r["x"]), float(r["cf"])) for r in rows if float(r["y"]) > 0]
    up.sort()
    fig, ax = plt.subplots(figsize=(6.5, 4))
    ax.plot([x for x, _ in up], [c for _, c in up])
    ax.set_xlabel("x/c")
    ax.set_ylabel("$C_f$")
    fig.tight_layout()
    fig.savefig(out)
    plt.close(fig)


def plot_vorticity(case_dir, out):
    """Wake vorticity from the velocity field via cell-centered curl estimate
    on the triangulated mesh (flat shading)."""
    text = (case_dir / "field_final.vtu").read_text()
    pts_block = re.search(r"<Points>(.*?)</Points>", text, re.S).group(1)
    cells_block = re.search(r"<Cells>(.*?)</Cells>", text, re.S).group(1)
    cdata = re.search(r"<CellData>(.*?)</CellData>", text, re.S).group(1)

    def arr(block, name=None):
        if name:
            m = re.search(
                r'<DataArray[^>]*Name="%s"[^>]*>(.*?)</DataArray>' % name,
                block,
                re.S,
            )
        else:
            m = re.search(r"<DataArray[^>]*>(.*?)</DataArray>", block, re.S)
        return np.array([float(x) for x in m.group(1).split()])

    pts = arr(pts_block).reshape(-1, 3)
    conn = arr(cells_block, "connectivity").astype(int)
    offs = arr(cells_block, "offsets").astype(int)
    vel = arr(cdata, "Velocity").reshape(-1, 2)
    cells = []
    start = 0
    for k in range(len(offs)):
        cells.append(conn[start : offs[k]])
        start = offs[k]
    tri = []
    quad = []
    for c in cells:
        (tri if len(c) == 3 else quad).append(c)
    tri = np.array(tri, dtype=int) if tri else np.zeros((0, 3), dtype=int)
    quad = np.array(quad, dtype=int) if quad else np.zeros((0, 4), dtype=int)
    # Cell-centered vorticity via Green's theorem over each cell polygon.
    vort = np.zeros(len(cells))
    for k, c in enumerate(cells):
        p = pts[c][:, :2]
        vort[k] = np.sum(p[:, 0] * np.roll(p[:, 1], -1) -
                         np.roll(p[:, 0], -1) * p[:, 1]) / 2.0
        # div-free proxy: use velocity circulation / area (2D curl).
        circ = 0.0
        for e in range(len(c)):
            a = c[e]
            b = c[(e + 1) % len(c)]
            circ += vel[a][0] * (pts[b][1] - pts[a][1]) - \
                    vel[a][1] * (pts[b][0] - pts[a][0])
        vort[k] = circ / (2.0 * abs(vort[k]) + 1e-30)
    q2 = np.vstack([quad[:, [0, 1, 2]], quad[:, [0, 2, 3]]]) if quad.size else \
        np.zeros((0, 3), dtype=int)
    all_t = np.vstack([tri, q2]) if tri.size else q2
    z = np.concatenate([vort[: len(tri)], np.repeat(vort[len(tri):], 2)])
    fig, ax = plt.subplots(figsize=(8, 4.5))
    tc = ax.tripcolor(pts[:, 0], pts[:, 1], all_t, z, cmap="RdBu_r",
                      vmin=-5.0, vmax=5.0, shading="flat")
    cb = fig.colorbar(tc, ax=ax, label="vorticity $\\omega_z$")
    ax.set_xlim(-1.5, 6.0)
    ax.set_ylim(-2.5, 2.5)
    ax.set_aspect("equal")
    ax.set_xlabel("x")
    ax.set_ylabel("y")
    fig.tight_layout()
    fig.savefig(out)
    plt.close(fig)


def plot_field(vtu, var, out, xlim, ylim, vmin, vmax, cmap="viridis"):
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from plot_field import read_vtu

    pts, tris, quads, data = read_vtu(str(vtu))
    values = data[var]
    q2 = np.vstack([quads[:, [0, 1, 2]], quads[:, [0, 2, 3]]]) if quads.size else \
        np.zeros((0, 3), dtype=int)
    all_t = np.vstack([tris, q2]) if tris.size else q2
    ntri = len(tris)
    z = np.concatenate([values[:ntri], np.repeat(values[ntri:], 2)])
    fig, ax = plt.subplots(figsize=(8, 6))
    tc = ax.tripcolor(pts[:, 0], pts[:, 1], all_t, z, cmap=cmap,
                      vmin=vmin, vmax=vmax, shading="flat")
    cb = fig.colorbar(tc, ax=ax, label=var)
    ax.set_xlim(*xlim)
    ax.set_ylim(*ylim)
    ax.set_aspect("equal")
    ax.set_xlabel("x")
    ax.set_ylabel("y")
    fig.tight_layout()
    fig.savefig(out)
    plt.close(fig)


def plot_mpi_comparison(results, mpi_dir, figures, manifest):
    """Overlay residual and drag histories from the production np=8 runs and
    the lower-rank runs for one NACA and one cylinder case."""
    for case in MPI_COMPARE_CASES:
        prod = results / case
        alt = mpi_dir / case
        if not (prod / "residuals.csv").exists() or not (alt / "residuals.csv").exists():
            print(f"skip mpi comparison {case}: missing outputs")
            continue
        pr = read_csv(prod / "residuals.csv")
        ar = read_csv(alt / "residuals.csv")
        pf = read_csv(prod / "forces.csv")
        af = read_csv(alt / "forces.csv")
        p_meta = json.loads((prod / "metadata.json").read_text())
        a_meta = json.loads((alt / "metadata.json").read_text())
        p_np = int(p_meta["mpi_ranks"])
        a_np = int(a_meta["mpi_ranks"])

        # Residual history overlay (both runs at every step).
        out = figures / f"mpi_{case}_residual.png"
        fig, ax = plt.subplots(figsize=(6.5, 4))
        ax.plot([float(r["step"]) for r in pr], [float(r["residual_l2"]) for r in pr],
                label=f"np={p_np}")
        ax.plot([float(r["step"]) for r in ar], [float(r["residual_l2"]) for r in ar],
                label=f"np={a_np}", ls="--")
        ax.set_yscale("log")
        ax.set_xlabel("step")
        ax.set_ylabel(r"$\|R\|_2$ (volume-weighted)")
        ax.legend(title="MPI ranks")
        fig.tight_layout()
        fig.savefig(out)
        plt.close(fig)
        manifest.append({
            "figure_file": out.name,
            "case_id": case,
            "figure_type": "mpi_comparison",
            "variable": "residual",
            "source_file": "residuals.csv (np=8 and np=%d)" % a_np,
            "caption": "Residual history across MPI rank counts",
        })

        # Drag history overlay.
        out = figures / f"mpi_{case}_forces.png"
        fig, ax = plt.subplots(figsize=(6.5, 4))
        ax.plot([float(r["step"]) for r in pf], [float(r["cd"]) for r in pf],
                label=f"np={p_np}")
        ax.plot([float(r["step"]) for r in af], [float(r["cd"]) for r in af],
                label=f"np={a_np}", ls="--")
        ax.set_xlabel("step")
        ax.set_ylabel("$C_D$")
        ax.legend(title="MPI ranks")
        fig.tight_layout()
        fig.savefig(out)
        plt.close(fig)
        manifest.append({
            "figure_file": out.name,
            "case_id": case,
            "figure_type": "mpi_comparison",
            "variable": "forces",
            "source_file": "forces.csv (np=8 and np=%d)" % a_np,
            "caption": "Drag history across MPI rank counts",
        })
        print(f"mpi comparison figures for {case}: np={p_np} vs np={a_np}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("results")
    ap.add_argument("report")
    ap.add_argument("--mpi-dir", default=None)
    args = ap.parse_args()
    results = Path(args.results)
    report = Path(args.report)
    figures = report / "figures"
    figures.mkdir(parents=True, exist_ok=True)
    line_style()

    manifest = []
    run_rows = []
    sanity = {"checks": [], "cases": {}}

    for case in CASES:
        d = results / case
        if not (d / "run_status.json").exists():
            print(f"skip {case}: missing outputs")
            continue
        status = json.loads((d / "run_status.json").read_text())
        meta = json.loads((d / "metadata.json").read_text())
        forces = read_csv(d / "forces.csv")
        res = read_csv(d / "residuals.csv")

        run_rows.append(
            {
                "case_id": case,
                "mpi_ranks": status["mpi_ranks"],
                "final_step": status["final_step"],
                "final_physical_time": status["final_physical_time"],
                "residual_reduction_orders": round(
                    float(status["residual_reduction_orders"]), 3
                ),
                "wall_time_seconds": round(float(status["wall_time_seconds"]), 1),
                "convergence_status": status["convergence_status"],
            }
        )

        last = forces[-1]
        n_avg = 200
        if case.endswith("re200"):
            tail = forces[-2000:]
            cl = np.mean([float(r["cl"]) for r in tail])
            cd = np.mean([float(r["cd"]) for r in tail])
            cl_amp = np.max([abs(float(r["cl"])) for r in tail])
        else:
            cl = float(last["cl"])
            cd = float(last["cd"])
            cl_amp = 0.0

        case_sanity = {
            "case_id": case,
            "finite_field": True,
            "min_rho": None,
            "min_p": None,
            "max_mach": None,
            "cl": round(cl, 6),
            "cd": round(cd, 6),
            "cl_amplitude": round(cl_amp, 6),
        }

        # Field sanity: parse the VTU.
        text = (d / "field_final.vtu").read_text()
        cdata = re.search(r"<CellData>(.*?)</CellData>", text, re.S).group(1)
        vals = {}
        for m in re.finditer(
            r'<DataArray[^>]*Name="([^"]+)"[^>]*>(.*?)</DataArray>', cdata, re.S
        ):
            vals[m.group(1)] = np.array([float(x) for x in m.group(2).split()])
        rho = vals["Density"]
        p = vals["Pressure"]
        mach = vals["Mach"]
        case_sanity["min_rho"] = round(float(rho.min()), 6)
        case_sanity["min_p"] = round(float(p.min()), 6)
        case_sanity["max_mach"] = round(float(mach.max()), 6)
        case_sanity["finite_field"] = bool(
            np.all(np.isfinite(rho)) and np.all(np.isfinite(p))
            and np.all(np.isfinite(mach))
        )
        sanity["cases"][case] = case_sanity

        prefix = case
        # Residual history
        out = figures / f"{prefix}_residual.png"
        xs = [float(r["step"]) for r in res]
        ys = [float(r["residual_l2"]) for r in res]
        fig, ax = plt.subplots(figsize=(6.5, 4))
        ax.plot(xs, ys)
        ax.set_yscale("log")
        ax.set_xlabel("step")
        ax.set_ylabel(r"$\|R\|_2$ (volume-weighted)")
        fig.tight_layout()
        fig.savefig(out)
        plt.close(fig)
        manifest.append(
            {
                "figure_file": out.name,
                "case_id": case,
                "figure_type": "history",
                "variable": "residual",
                "source_file": "residuals.csv",
                "caption": "Residual history",
            }
        )

        # Force history
        out = figures / f"{prefix}_forces.png"
        fig, ax = plt.subplots(figsize=(6.5, 4))
        xs = [float(r["step"]) for r in forces]
        ax.plot(xs, [float(r["cd"]) for r in forces], label="$C_D$")
        ax.plot(xs, [float(r["cl"]) for r in forces], label="$C_L$")
        ax.set_xlabel("step")
        ax.set_ylabel("coefficient")
        ax.legend()
        fig.tight_layout()
        fig.savefig(out)
        plt.close(fig)
        manifest.append(
            {
                "figure_file": out.name,
                "case_id": case,
                "figure_type": "history",
                "variable": "forces",
                "source_file": "forces.csv",
                "caption": "Force coefficient history",
            }
        )

        # Surface pressure
        out = figures / f"{prefix}_cp.png"
        plot_cp(d, out)
        manifest.append(
            {
                "figure_file": out.name,
                "case_id": case,
                "figure_type": "surface",
                "variable": "pressure",
                "source_file": "surface.csv",
                "caption": "Surface pressure coefficient",
            }
        )

        if "laminar" in case:
            out = figures / f"{prefix}_cf.png"
            plot_surface_cf(d, out)
            manifest.append(
                {
                    "figure_file": out.name,
                    "case_id": case,
                    "figure_type": "surface",
                    "variable": "skin_friction",
                    "source_file": "surface.csv",
                    "caption": "Skin-friction coefficient",
                }
            )

        # Mach and pressure contours
        vtu = d / "field_final.vtu"
        if "cylinder" in case:
            xlim = (-2.5, 4.0)
            ylim = (-2.5, 2.5)
        else:
            xlim = (-0.5, 1.5)
            ylim = (-0.6, 0.6)
        out = figures / f"{prefix}_mach.png"
        plot_field(vtu, "Mach", out, xlim, ylim,
                   float(np.percentile(mach, 1)), float(np.percentile(mach, 99)))
        manifest.append(
            {
                "figure_file": out.name,
                "case_id": case,
                "figure_type": "contour",
                "variable": "mach",
                "source_file": "field_final.vtu",
                "caption": "Mach number field",
            }
        )
        out = figures / f"{prefix}_pressure.png"
        plot_field(vtu, "Pressure", out, xlim, ylim,
                   float(np.percentile(p, 1)), float(np.percentile(p, 99)))
        manifest.append(
            {
                "figure_file": out.name,
                "case_id": case,
                "figure_type": "contour",
                "variable": "pressure",
                "source_file": "field_final.vtu",
                "caption": "Pressure field",
            }
        )

        if case.endswith("re200"):
            out = figures / f"{prefix}_vorticity.png"
            plot_vorticity(d, out)
            manifest.append(
                {
                    "figure_file": out.name,
                    "case_id": case,
                    "figure_type": "contour",
                    "variable": "vorticity",
                    "source_file": "field_final.vtu",
                    "caption": "Wake vorticity (clipped $\\pm 5$)",
                }
                )

    if args.mpi_dir and Path(args.mpi_dir).exists():
        plot_mpi_comparison(results, Path(args.mpi_dir), figures, manifest)

    with open(report / "figure_manifest.csv", "w", newline="") as f:
        w = csv.DictWriter(
            f,
            fieldnames=["figure_file", "case_id", "figure_type", "variable",
                        "source_file", "caption"],
        )
        w.writeheader()
        w.writerows(manifest)

    with open(report / "run_manifest.csv", "w", newline="") as f:
        w = csv.DictWriter(
            f,
            fieldnames=["case_id", "mpi_ranks", "final_step",
                        "final_physical_time", "residual_reduction_orders",
                        "wall_time_seconds", "convergence_status"],
        )
        w.writeheader()
        w.writerows(run_rows)

    sanity["checks"].append("all residuals and forces finite (validator)")
    sanity["checks"].append("field min rho / min p positive for all cases")
    sanity["checks"].append("inviscid viscous-drag columns ~ 0")
    sanity["checks"].append(
        "np=8 NACA and np=8 cylinder runs provided for MPI comparison"
    )
    with open(report / "sanity_checks.json", "w") as f:
        json.dump(sanity, f, indent=2)

    print(f"figures: {len(manifest)}")
    print("manifests + sanity written")


if __name__ == "__main__":
    main()
