"""Generate all report figures + figure_manifest.csv from fv2d result dirs.

Usage: plot_all.py <results_root> <report_dir> [--consistency <dir1,dir2,...>]
"""
from __future__ import annotations

import argparse
import csv
import json
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.tri as mtri
import numpy as np

sys.path.insert(0, str(Path(__file__).parent))
from cfdpost import VtkField, col, load_case, read_csv_rows, strouhal_from_lift

plt.rcParams.update(
    {
        "font.size": 11,
        "axes.grid": True,
        "grid.alpha": 0.3,
        "lines.linewidth": 1.4,
        "figure.dpi": 150,
        "savefig.bbox": "tight",
    }
)

MANIFEST = []


def reg(figfile, case_id, ftype, variable, source, caption):
    MANIFEST.append(
        {
            "figure_file": figfile,
            "case_id": case_id,
            "figure_type": ftype,
            "variable": variable,
            "source_file": source,
            "caption": caption,
        }
    )


def save(fig, outdir, name, *manifest_args):
    fig.savefig(outdir / "figures" / name)
    plt.close(fig)
    reg(name, *manifest_args)


def plot_residuals(case, cid, outdir):
    rows = case["residuals"]
    step = col(rows, "step", int)
    fig, ax = plt.subplots(figsize=(7, 4.5))
    for k, lbl in [("rho", r"$\rho$"), ("rhou", r"$\rho u$"), ("rhov", r"$\rho v$"),
                   ("rhoE", r"$\rho E$"), ("residual_l2", "combined L2")]:
        v = col(rows, k)
        v = np.maximum(v, 1e-300)
        ax.semilogy(step, v / v[0], label=lbl)
    ax.set_xlabel("pseudo-time step" if case["status"]["final_physical_time"] == 0 else "physical step")
    ax.set_ylabel("residual / initial")
    ax.set_title(f"Residual history — {cid}")
    ax.legend()
    save(fig, outdir, f"{cid}_residuals.png", cid, "residual_history", "residual_l2",
         str(case["dir"] / "residuals.csv"),
         f"Residual history (normalized) for {cid}")


def plot_forces(case, cid, outdir):
    rows = case["forces"]
    transient = case["status"]["final_physical_time"] > 0
    x = col(rows, "physical_time") if transient else col(rows, "step", int)
    cl = col(rows, "cl")
    cd = col(rows, "cd")
    fig, ax = plt.subplots(figsize=(7, 4.5))
    ax.plot(x, cl, label="$C_l$")
    ax.plot(x, cd, label="$C_d$")
    # clip y-range to the settled part so the startup spike does not dominate
    tail = np.concatenate([cl[int(len(cl) * 0.7) :], cd[int(len(cd) * 0.7) :]])
    lo, hi = np.percentile(tail, [0.5, 99.5])
    pad = 0.25 * max(hi - lo, 1e-4)
    ax.set_ylim(lo - pad, hi + pad)
    ax.set_xlabel("physical time" if transient else "pseudo-time step")
    ax.set_ylabel("force coefficient")
    ax.set_title(f"Force history — {cid}")
    ax.legend()
    save(fig, outdir, f"{cid}_forces.png", cid, "force_history", "cl_cd",
         str(case["dir"] / "forces.csv"), f"Lift and drag history for {cid}")


def plot_surface(case, cid, outdir):
    rows = case["surface"]
    x = col(rows, "x")
    y = col(rows, "y")
    cp = col(rows, "cp")
    cylinder = "cylinder" in cid
    fig, ax = plt.subplots(figsize=(7, 4.5))
    if cylinder:
        ang = np.degrees(np.arctan2(y, x))
        order = np.argsort(ang)
        ax.plot(ang[order], cp[order], ".", ms=3, label="$C_p$")
        ax.set_xlabel("angle [deg]")
        ax.set_ylabel("$C_p$")
        ax.set_title(f"Cylinder wall pressure coefficient — {cid}")
    else:
        order = np.argsort(x)
        ax.plot(x[order], cp[order], ".", ms=3)
        ax.set_xlabel("x/c")
        ax.set_ylabel("$C_p$")
        ax.invert_yaxis()
        ax.set_title(f"Surface pressure coefficient — {cid}")
    save(fig, outdir, f"{cid}_surface_cp.png", cid, "surface_distribution", "cp",
         str(case["dir"] / "surface.csv"), f"Surface Cp distribution for {cid}")
    if cylinder:
        cf = col(rows, "cf")
        fig, ax = plt.subplots(figsize=(7, 4.5))
        ax.plot(ang[order], cf[order], ".", ms=3, color="darkred")
        ax.set_xlabel("angle [deg]")
        ax.set_ylabel("$C_f$")
        ax.set_title(f"Cylinder wall skin friction — {cid}")
        save(fig, outdir, f"{cid}_surface_cf.png", cid, "surface_distribution", "cf",
             str(case["dir"] / "surface.csv"), f"Surface Cf distribution for {cid}")
    elif "laminar" in cid:
        cf = col(rows, "cf")
        fig, ax = plt.subplots(figsize=(7, 4.5))
        ax.plot(x[order], cf[order], ".", ms=3, color="darkred")
        ax.set_xlabel("x/c")
        ax.set_ylabel("$C_f$")
        ax.set_title(f"Wall skin-friction coefficient — {cid}")
        save(fig, outdir, f"{cid}_surface_cf.png", cid, "surface_distribution", "cf",
             str(case["dir"] / "surface.csv"), f"Surface Cf distribution for {cid}")


def contour(case, cid, outdir, varname, pretty_name, fname_var, zoom):
    field: VtkField = case["field"]
    vals = field.cell_data[varname]
    node_vals = field.node_value(vals)
    tri = mtri.Triangulation(field.points[:, 0], field.points[:, 1], field.tris)
    fig, ax = plt.subplots(figsize=(7.5, 5.5))
    # clipped percentile range for robustness
    lo, hi = np.percentile(vals, [0.5, 99.5])
    if hi - lo < 1e-12:
        hi = lo + 1e-12
    levels = np.linspace(lo, hi, 41)
    cf = ax.tricontourf(tri, node_vals, levels=levels, cmap="jet")
    ax.tricontour(tri, node_vals, levels=levels[::4], colors="k", linewidths=0.15, alpha=0.4)
    cb = fig.colorbar(cf, ax=ax, shrink=0.85)
    cb.set_label(pretty_name)
    ax.set_aspect("equal")
    if zoom:
        if "cylinder" in cid:
            ax.set_xlim(-1.0, 6.0)
            ax.set_ylim(-2.5, 2.5)
        else:
            ax.set_xlim(-0.2, 1.2)
            ax.set_ylim(-0.6, 0.6)
        suffix = "_zoom"
        title = f"{pretty_name} (near-body) — {cid}"
    else:
        # meaningful window: not the entire 200D farfield
        if "cylinder" in cid:
            ax.set_xlim(-5, 15)
            ax.set_ylim(-6, 6)
        else:
            ax.set_xlim(-2, 4)
            ax.set_ylim(-3, 3)
        suffix = ""
        title = f"{pretty_name} — {cid}"
    ax.set_title(title)
    ax.set_xlabel("x")
    ax.set_ylabel("y")
    name = f"{cid}_{fname_var}{suffix}.png"
    fig.savefig(outdir / "figures" / name)
    plt.close(fig)
    reg(name, cid, "field_contour", varname, str(case["dir"] / "field_final.vtk"),
        f"{title}; filled contours of {pretty_name} from final field")


def plot_partition(case, cid, outdir):
    field: VtkField = case["field"]
    vals = field.cell_data["rank"]
    tri = mtri.Triangulation(field.points[:, 0], field.points[:, 1], field.tris)
    fig, ax = plt.subplots(figsize=(7.5, 5.5))
    cf = ax.tripcolor(tri, field.node_value(vals), cmap="tab10", shading="flat")
    cb = fig.colorbar(cf, ax=ax, shrink=0.85, ticks=np.unique(vals))
    cb.set_label("MPI rank")
    ax.set_aspect("equal")
    if "cylinder" in cid:
        ax.set_xlim(-2, 8)
        ax.set_ylim(-4, 4)
    else:
        ax.set_xlim(-1, 3)
        ax.set_ylim(-2, 2)
    ax.set_title(f"METIS partitioning ({int(case['metadata']['mpi_ranks'])} ranks) — {cid}")
    save(fig, outdir, f"{cid}_partition.png", cid, "partition", "rank",
         str(case["dir"] / "field_final.vtk"), f"Rank ownership map for {cid}")


def plot_re200_wake(case, cid, outdir, results_root):
    # use a post-transient intermediate field if available, else final
    d = case["dir"]
    inter = sorted(d.glob("field_t*.vtk"))
    path = inter[-2] if len(inter) >= 2 else (d / "field_final.vtk")
    field = VtkField(path)
    vals = field.cell_data["vorticity"]
    node_vals = field.node_value(vals)
    tri = mtri.Triangulation(field.points[:, 0], field.points[:, 1], field.tris)
    fig, ax = plt.subplots(figsize=(8, 5))
    clip = 5.0
    levels = np.linspace(-clip, clip, 41)
    cf = ax.tricontourf(tri, np.clip(node_vals, -clip, clip), levels=levels, cmap="RdBu_r")
    cb = fig.colorbar(cf, ax=ax, shrink=0.85)
    cb.set_label(r"vorticity $\omega_z$ (clipped to [-5,5])")
    ax.set_aspect("equal")
    ax.set_xlim(-1, 12)
    ax.set_ylim(-4, 4)
    ax.set_title(f"Post-transient wake vorticity — {cid}")
    save(fig, outdir, f"{cid}_vorticity_wake.png", cid, "wake_visualization", "vorticity",
         str(path), f"Vortex street wake (clipped vorticity) for {cid}")


def plot_case(case_dir, outdir):
    case = load_case(case_dir)
    cid = case["metadata"]["case_id"]
    plot_residuals(case, cid, outdir)
    plot_forces(case, cid, outdir)
    plot_surface(case, cid, outdir)
    if "field" not in case:
        return cid
    contour(case, cid, outdir, "mach", "Mach number", "mach", zoom=False)
    contour(case, cid, outdir, "mach", "Mach number", "mach", zoom=True)
    contour(case, cid, outdir, "pressure", "pressure", "pressure", zoom=False)
    contour(case, cid, outdir, "pressure", "pressure", "pressure", zoom=True)
    plot_partition(case, cid, outdir)
    if "re200" in cid:
        plot_re200_wake(case, cid, outdir, None)
    return cid


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("results_root")
    ap.add_argument("report_dir")
    args = ap.parse_args()
    root = Path(args.results_root)
    outdir = Path(args.report_dir)
    (outdir / "figures").mkdir(parents=True, exist_ok=True)
    for d in sorted(root.iterdir()):
        if d.is_dir() and (d / "metadata.json").exists():
            print("plotting", d.name)
            plot_case(d, outdir)
    with open(outdir / "figure_manifest.csv", "w", newline="") as f:
        w = csv.DictWriter(
            f, fieldnames=["figure_file", "case_id", "figure_type", "variable",
                           "source_file", "caption"]
        )
        w.writeheader()
        w.writerows(MANIFEST)
    print("wrote", outdir / "figure_manifest.csv", len(MANIFEST), "entries")


if __name__ == "__main__":
    main()
