#!/usr/bin/env python3
"""Generate publication-style figures for the 2-D CFD solver benchmark.

Reads each case's result files (residuals.csv, forces.csv, surface.csv and the
VTK field files field_final.pvtu / field_final_p*.vtu) from --results-dir and
writes one PNG per figure into --figures-dir:

    {case_id}_residuals.png   L2/Linf residual history
    {case_id}_forces.png      lift/drag coefficient history
    {case_id}_surface_cp.png  surface pressure coefficient distribution
    {case_id}_mach.png        Mach-number filled contour (near-body zoom)
    {case_id}_pressure.png    pressure filled contour (near-body zoom)
    {case_id}_vorticity.png   clipped vorticity wake (cylinder Re200 only)

A figure manifest (figure_manifest.csv) mapping every figure to its source
data is written next to the figures directory.

Usage:
    python plot_results.py --results-dir <path> --figures-dir <path> \
        [--case-id <id>] [--manifest-out <path>]
"""

import argparse
import csv
import json
import math
import os
import re
import sys

import numpy as np

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.tri as mtri

import pyvista as pv

# ---------------------------------------------------------------------------
# Case metadata / styling
# ---------------------------------------------------------------------------

CASE_TITLES = {
    "naca0012_m015_inviscid": "NACA0012, $M_\\infty = 0.15$, inviscid",
    "naca0012_m015_laminar_re5000": "NACA0012, $M_\\infty = 0.15$, laminar, $\\mathrm{Re} = 5000$",
    "naca0012_m080_inviscid": "NACA0012, $M_\\infty = 0.8$, inviscid (not converged)",
    "naca0012_m080_laminar_re5000": "NACA0012, $M_\\infty = 0.8$, laminar, $\\mathrm{Re} = 5000$",
    "naca0012_m200_inviscid": "NACA0012, $M_\\infty = 2.0$, inviscid",
    "naca0012_m200_laminar_re5000": "NACA0012, $M_\\infty = 2.0$, laminar, $\\mathrm{Re} = 5000$",
    "cylinder_m010_laminar_re20": "Circular cylinder, $M_\\infty = 0.1$, laminar, $\\mathrm{Re} = 20$",
    "cylinder_m010_laminar_re200": "Circular cylinder, $M_\\infty = 0.1$, laminar, $\\mathrm{Re} = 200$ (transient)",
}

# Near-body view bounds: [xmin, xmax, ymin, ymax]. The NACA far field is 80
# chords away and the cylinder far field 200 diameters away, so an unrestricted
# view would hide the body entirely.
NACA_VIEW = (-0.75, 1.75, -1.0, 1.0)
CYLINDER_VIEW = (-1.5, 3.0, -2.25, 2.25)

WALL_TAGS = {"bc-4", "WALL", "wall"}


def case_title(case_id):
    return CASE_TITLES.get(case_id, case_id)


def setup_style():
    """Apply the scholarly matplotlib style (with a safe fallback)."""
    try:
        plt.style.use("seaborn-v0_8-whitegrid")
    except OSError:
        plt.style.use("ggplot")
    plt.rcParams.update(
        {
            "figure.dpi": 150,
            "savefig.dpi": 150,
            "font.size": 10,
            "axes.titlesize": 14,
            "axes.labelsize": 12,
            "xtick.labelsize": 10,
            "ytick.labelsize": 10,
            "legend.fontsize": 10,
            "axes.grid": True,
            "grid.alpha": 0.35,
            "grid.linewidth": 0.6,
            "axes.axisbelow": True,
            "lines.linewidth": 1.3,
        }
    )


# ---------------------------------------------------------------------------
# Data loading
# ---------------------------------------------------------------------------


def load_numeric_csv(path, required):
    """Load an all-numeric CSV with a header row via numpy."""
    data = np.genfromtxt(path, delimiter=",", names=True, dtype=float)
    for col in required:
        if col not in data.dtype.names:
            raise KeyError(f"{os.path.basename(path)}: missing column '{col}'")
    return data


def load_surface_csv(path):
    """Load surface.csv (mixed numeric/string columns) as a list of dicts."""
    with open(path, newline="") as f:
        return [dict(row) for row in csv.DictReader(f)]


def find_field_file(case_dir):
    """Locate the VTK field file: .pvtu preferred, then .vtu, then rank 0."""
    for name in ("field_final.pvtu", "field_final.vtu", "field_final_p0.vtu"):
        p = os.path.join(case_dir, name)
        if os.path.exists(p):
            return p
    return None


def read_field(case_dir):
    path = find_field_file(case_dir)
    if path is None:
        raise FileNotFoundError("no VTK field file in " + case_dir)
    mesh = pv.read(path)
    if mesh.n_cells == 0:
        raise RuntimeError(f"field file {path} contains no cells")
    return mesh


def triangle_connectivity(mesh):
    """Flatten an unstructured 2-D mesh (TRI_3/QUAD_4 cells) into triangles.

    Returns (points, triangles) where points is (N, 2) and triangles is the
    index array for matplotlib.tri.Triangulation.
    """
    conn = np.asarray(mesh.cell_connectivity)
    ct = np.asarray(mesh.celltypes)
    npts = {5: 3, 9: 4}  # VTK_TRIANGLE=5, VTK_QUAD=9
    tris = []
    pos = 0
    for i in range(mesh.n_cells):
        k = npts.get(int(ct[i]))
        if k is None:
            continue
        idx = conn[pos : pos + k]
        pos += k
        if k == 3:
            tris.append(idx)
        else:
            tris.append([idx[0], idx[1], idx[2]])
            tris.append([idx[0], idx[2], idx[3]])
    if not tris:
        raise RuntimeError("mesh has no TRI_3/QUAD_4 cells to triangulate")
    return np.asarray(mesh.points)[:, :2], np.asarray(tris, dtype=int)


def cell_to_point(mesh, name):
    """Average a cell-data array onto mesh points."""
    return np.asarray(mesh.cell_data_to_point_data()[name], dtype=float)


def compute_vorticity(mesh):
    """Cell-centered out-of-plane vorticity omega_z = dv/dx - du/dy."""
    gu = np.asarray(mesh.compute_derivative(scalars="velocity_x")["gradient"])
    gv = np.asarray(mesh.compute_derivative(scalars="velocity_y")["gradient"])
    return gv[:, 0] - gu[:, 1]


# ---------------------------------------------------------------------------
# Figure writers
# ---------------------------------------------------------------------------


def plot_residuals(case_dir, out_path, case_id):
    r = load_numeric_csv(os.path.join(case_dir, "residuals.csv"), ["step", "residual_l2", "residual_linf"])
    fig, ax = plt.subplots(figsize=(6, 4))
    tiny = np.finfo(float).tiny
    ax.semilogy(r["step"], np.maximum(r["residual_l2"], tiny), label=r"$L_2$ residual")
    ax.semilogy(r["step"], np.maximum(r["residual_linf"], tiny), label=r"$L_\infty$ residual")
    ax.set_xlabel("Pseudo-time step")
    ax.set_ylabel("Residual norm (nondimensional)")
    ax.set_title(f"{case_title(case_id)}: residual history")
    ax.legend(loc="best")
    fig.tight_layout()
    fig.savefig(out_path)
    plt.close(fig)


def plot_forces(case_dir, out_path, case_id):
    f = load_numeric_csv(os.path.join(case_dir, "forces.csv"), ["step", "cl", "cd"])
    fig, ax = plt.subplots(figsize=(6, 4))
    ax.plot(f["step"], f["cd"], color="tab:blue", label=r"$C_D$")
    ax.set_xlabel("Step")
    ax.set_ylabel(r"Drag coefficient $C_D$")
    ax2 = ax.twinx()
    ax2.plot(f["step"], f["cl"], color="tab:red", label=r"$C_L$")
    ax2.set_ylabel(r"Lift coefficient $C_L$")
    ax.set_title(f"{case_title(case_id)}: force history")
    lines1, labels1 = ax.get_legend_handles_labels()
    lines2, labels2 = ax2.get_legend_handles_labels()
    ax.legend(lines1 + lines2, labels1 + labels2, loc="best")
    fig.tight_layout()
    fig.savefig(out_path)
    plt.close(fig)


def plot_surface_cp(case_dir, out_path, case_id):
    rows = [r for r in load_surface_csv(os.path.join(case_dir, "surface.csv")) if r["tag"] in WALL_TAGS]
    if not rows:
        raise ValueError(f"{case_id}: no wall rows in surface.csv")
    fig, ax = plt.subplots(figsize=(6, 4))
    if case_id.startswith("naca"):
        x = np.array([float(r["x"]) for r in rows])
        cp = np.array([float(r["cp"]) for r in rows])
        order = np.argsort(x)
        ax.plot(x[order], cp[order], "o", ms=2.5, color="tab:blue", label=r"$C_p$")
        ax.set_xlabel("Chordwise position $x/c$")
        ax.set_ylabel(r"Pressure coefficient $C_p$")
        title_txt = "surface pressure coefficient"
        if "cf" in rows[0]:
            cf = np.array([float(r["cf"]) for r in rows])
            ax2 = ax.twinx()
            ax2.plot(x[order], cf[order], "s", ms=2.5, color="tab:red", label=r"$C_f$")
            ax2.set_ylabel(r"Skin-friction coefficient $C_f$")
            lines1, labels1 = ax.get_legend_handles_labels()
            lines2, labels2 = ax2.get_legend_handles_labels()
            ax.legend(lines1 + lines2, labels1 + labels2, loc="best")
            title_txt = "surface pressure and skin-friction coefficients"
    else:
        theta = np.degrees(np.arctan2([float(r["y"]) for r in rows], [float(r["x"]) for r in rows]))
        cp = np.array([float(r["cp"]) for r in rows])
        order = np.argsort(theta)
        ax.plot(theta[order], cp[order], "o", ms=2.5, color="tab:blue")
        ax.set_xlabel("Angle around cylinder $\\theta$ (deg)")
        ax.set_ylabel(r"Pressure coefficient $C_p$")
        title_txt = "surface pressure coefficient"
    ax.set_title(f"{case_title(case_id)}: {title_txt}")
    fig.tight_layout()
    fig.savefig(out_path)
    plt.close(fig)


def plot_field_contour(case_dir, out_path, case_id, var, label, view, cmap="viridis",
                       vmin=None, vmax=None, levels=50):
    mesh = read_field(case_dir)
    pts, tris = triangle_connectivity(mesh)
    vals = cell_to_point(mesh, var)
    mask = (
        (pts[:, 0] >= view[0]) & (pts[:, 0] <= view[1])
        & (pts[:, 1] >= view[2]) & (pts[:, 1] <= view[3])
    )
    keep = mask[tris].all(axis=1)
    tri = mtri.Triangulation(pts[:, 0], pts[:, 1], tris[keep])
    fig, ax = plt.subplots(figsize=(8, 5))
    tcf = ax.tricontourf(tri, vals, levels=levels, cmap=cmap, vmin=vmin, vmax=vmax)
    cb = fig.colorbar(tcf, ax=ax, shrink=0.92)
    cb.set_label(label)
    ax.set_aspect("equal")
    ax.set_xlabel("$x/c$" if case_id.startswith("naca") else "$x/D$")
    ax.set_ylabel("$y/c$" if case_id.startswith("naca") else "$y/D$")
    ax.set_title(f"{case_title(case_id)}: {label} (near-body view)")
    fig.tight_layout()
    fig.savefig(out_path)
    plt.close(fig)


def plot_vorticity(case_dir, out_path, case_id):
    mesh = attach_vorticity_to_mesh(read_field(case_dir))
    pts, tris = triangle_connectivity(mesh)
    vort = cell_to_point(mesh, "_vorticity_z")
    mask = (
        (pts[:, 0] >= CYLINDER_VIEW[0]) & (pts[:, 0] <= CYLINDER_VIEW[1])
        & (pts[:, 1] >= CYLINDER_VIEW[2]) & (pts[:, 1] <= CYLINDER_VIEW[3])
    )
    keep = mask[tris].all(axis=1)
    tri = mtri.Triangulation(pts[:, 0], pts[:, 1], tris[keep])
    fig, ax = plt.subplots(figsize=(8, 5))
    tcf = ax.tricontourf(tri, vort, levels=50, cmap="RdBu_r", vmin=-5.0, vmax=5.0)
    cb = fig.colorbar(tcf, ax=ax, shrink=0.92)
    cb.set_label(r"Vorticity $\omega_z$ (nondimensional, clipped $[-5, 5]$)")
    ax.set_aspect("equal")
    ax.set_xlabel("$x/D$")
    ax.set_ylabel("$y/D$")
    ax.set_title(f"{case_title(case_id)}: vorticity wake")
    fig.tight_layout()
    fig.savefig(out_path)
    plt.close(fig)


def attach_vorticity_to_mesh(mesh):
    """Add computed cell vorticity to the mesh as a cell array."""
    mesh = mesh.copy()
    mesh.cell_data["_vorticity_z"] = compute_vorticity(mesh)
    return mesh


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def generate_case(case_dir, case_id, figures_dir, manifest):
    """Generate every figure possible for one case; append to the manifest."""
    generated = []

    def out(name):
        return os.path.join(figures_dir, f"{case_id}_{name}.png")

    # 1. Residual history ---------------------------------------------------
    res_path = os.path.join(case_dir, "residuals.csv")
    if os.path.exists(res_path):
        plot_residuals(case_dir, out("residuals"), case_id)
        generated.append(("residuals", "residual_l2/residual_linf", "residuals.csv",
                          f"Residual history for {case_title(case_id)}: $L_2$ and $L_\\infty$ "
                          "norms of the conservative residual versus pseudo-time step."))
    else:
        print(f"[warn] {case_id}: no residuals.csv, skipping residual figure")

    # 2. Force history ------------------------------------------------------
    frc_path = os.path.join(case_dir, "forces.csv")
    if os.path.exists(frc_path):
        plot_forces(case_dir, out("forces"), case_id)
        generated.append(("forces", "cl/cd", "forces.csv",
                          f"Force history for {case_title(case_id)}: lift $C_L$ and drag $C_D$ "
                          "coefficients versus step."))
    else:
        print(f"[warn] {case_id}: no forces.csv, skipping force figure")

    # 3. Surface pressure coefficient --------------------------------------
    srf_path = os.path.join(case_dir, "surface.csv")
    if os.path.exists(srf_path):
        try:
            plot_surface_cp(case_dir, out("surface_cp"), case_id)
            if case_id.startswith("naca"):
                xlab = "chordwise position $x/c$"
            else:
                xlab = "angle $\\theta$ around the cylinder"
            generated.append(("surface_cp", "cp", "surface.csv",
                              f"Surface pressure coefficient distribution for {case_title(case_id)} "
                              f"versus {xlab}."))
        except Exception as exc:  # noqa: BLE001
            print(f"[warn] {case_id}: surface_cp figure failed: {exc}")
    else:
        print(f"[warn] {case_id}: no surface.csv, skipping surface figure")

    # 4/5. Mach and pressure contours --------------------------------------
    if find_field_file(case_dir):
        try:
            mesh = read_field(case_dir)
            view = NACA_VIEW if case_id.startswith("naca") else CYLINDER_VIEW
            if "mach" in mesh.cell_data:
                plot_field_contour(case_dir, out("mach"), case_id, "mach",
                                   "Mach number $M$", view)
                generated.append(("mach", "mach", "field_final.pvtu",
                                  f"Mach-number contours near the body for {case_title(case_id)}; "
                                  "filled contour from the final converged field."))
            if "pressure" in mesh.cell_data:
                plot_field_contour(case_dir, out("pressure"), case_id, "pressure",
                                   r"Pressure $p/(\rho_\infty u_\infty^2)$", view)
                generated.append(("pressure", "pressure", "field_final.pvtu",
                                  f"Pressure contours near the body for {case_title(case_id)}; "
                                  "filled contour from the final converged field."))
        except Exception as exc:  # noqa: BLE001
            print(f"[warn] {case_id}: field contour figures failed: {exc}")
    else:
        print(f"[warn] {case_id}: no VTK field file, skipping contour figures")

    # 6. Vorticity wake (Re200 only) ---------------------------------------
    if "re200" in case_id and find_field_file(case_dir):
        try:
            plot_vorticity(case_dir, out("vorticity"), case_id)
            generated.append(("vorticity", "vorticity_z", "field_final.pvtu",
                              f"Out-of-plane vorticity $\\omega_z = \\partial v/\\partial x - "
                              f"\\partial u/\\partial y$ for {case_title(case_id)}, clipped to "
                              "$[-5, 5]$ to expose the vortex wake."))
        except Exception as exc:  # noqa: BLE001
            print(f"[warn] {case_id}: vorticity figure failed: {exc}")

    for ftype, var, source, caption in generated:
        fig_file = f"{case_id}_{ftype}.png"
        manifest.append({
            "figure_file": fig_file,
            "case_id": case_id,
            "figure_type": ftype,
            "variable": var,
            "source_file": source,
            "caption": caption,
        })
        print(f"[ok] {fig_file}")
    return len(generated)


def write_manifest(manifest, manifest_out):
    with open(manifest_out, "w", newline="") as f:
        writer = csv.DictWriter(
            f, fieldnames=["figure_file", "case_id", "figure_type", "variable", "source_file", "caption"]
        )
        writer.writeheader()
        writer.writerows(manifest)
    print(f"[manifest] {len(manifest)} figures -> {manifest_out}")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--results-dir", required=True, help="directory containing one subdir per case")
    ap.add_argument("--figures-dir", required=True, help="output directory for PNG figures")
    ap.add_argument("--case-id", default=None, help="only process this case (default: all)")
    ap.add_argument("--manifest-out", default=None,
                    help="figure_manifest.csv path (default: next to --figures-dir)")
    args = ap.parse_args()

    setup_style()
    os.makedirs(args.figures_dir, exist_ok=True)

    if args.case_id:
        case_ids = [args.case_id]
    else:
        case_ids = sorted(
            d for d in os.listdir(args.results_dir)
            if os.path.isdir(os.path.join(args.results_dir, d))
        )
    if not case_ids:
        print(f"[error] no case directories found under {args.results_dir}")
        sys.exit(1)

    manifest = []
    total = 0
    for case_id in case_ids:
        case_dir = os.path.join(args.results_dir, case_id)
        n = generate_case(case_dir, case_id, args.figures_dir, manifest)
        total += n

    manifest_out = args.manifest_out or os.path.join(os.path.dirname(os.path.abspath(args.figures_dir)),
                                                     "figure_manifest.csv")
    write_manifest(manifest, manifest_out)
    print(f"[done] {total} figures for {len(case_ids)} cases -> {args.figures_dir}")


if __name__ == "__main__":
    main()
