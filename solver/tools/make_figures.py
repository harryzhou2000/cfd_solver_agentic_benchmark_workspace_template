#!/usr/bin/env python3
"""Generate all report figures from the case result directories."""
import csv
import json
import math
import os
import sys

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.tri as mtri

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from plot_field import read_vtu, cell_triangulation

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
RESULTS = os.path.join(ROOT, "results")
FIGDIR = os.path.join(ROOT, "report", "figures")
os.makedirs(FIGDIR, exist_ok=True)

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

manifest = []


def add_fig(fname, case, ftype, var, source, caption):
    manifest.append({
        "figure_file": fname,
        "case_id": case,
        "figure_type": ftype,
        "variable": var,
        "source_file": source,
        "caption": caption,
    })


def load_csv(case, name):
    p = os.path.join(RESULTS, case, name)
    if not os.path.exists(p):
        return None
    with open(p, newline="") as f:
        return list(csv.DictReader(f))


def field_path(case):
    p = os.path.join(RESULTS, case, "field_final.vtu")
    return p if os.path.exists(p) else None


def plot_history(case):
    res = load_csv(case, "residuals.csv")
    frc = load_csv(case, "forces.csv")
    if not res or not frc:
        return
    fig, ax = plt.subplots(1, 2, figsize=(11, 4))
    t = [float(r["step"]) for r in res]
    l2 = [float(r["residual_l2"]) for r in res]
    ax[0].semilogy(t, l2, lw=0.8)
    ax[0].set_xlabel("iteration step")
    ax[0].set_ylabel(r"$L_2$ residual")
    ax[0].set_title("Residual history")
    ax[0].grid(True, which="both", alpha=0.3)
    ft = [float(r["step"]) for r in frc]
    cd = [float(r["cd"]) for r in frc]
    cl = [float(r["cl"]) for r in frc]
    ax[1].plot(ft, cd, lw=0.8, label=r"$C_d$")
    ax[1].plot(ft, cl, lw=0.8, label=r"$C_l$")
    ax[1].set_xlabel("iteration step")
    ax[1].legend()
    ax[1].set_title("Force history")
    ax[1].grid(True, alpha=0.3)
    fname = f"{case}_history.png"
    fig.tight_layout()
    fig.savefig(os.path.join(FIGDIR, fname), dpi=150)
    plt.close(fig)
    add_fig(fname, case, "history", "residual_l2,cd,cl", f"results/{case}/residuals.csv",
            f"{case}: residual and force histories")


def plot_surface(case):
    surf = load_csv(case, "surface.csv")
    if not surf:
        return
    is_naca = case.startswith("naca")
    is_cyl = case.startswith("cylinder")
    fig, ax = plt.subplots(figsize=(7, 4.6))
    if is_naca:
        xs = [float(r["x"]) for r in surf]
        cp = [float(r["cp"]) for r in surf]
        ax.plot(xs, cp, ".", ms=3)
        ax.set_xlabel("x/c")
        ax.set_ylabel(r"$C_p$")
        ax.set_title(f"{case}: surface pressure coefficient")
        ax.invert_yaxis()
        ylab = "cp"
    else:
        th = [math.degrees(math.atan2(float(r["y"]), float(r["x"]))) for r in surf]
        cp = [float(r["cp"]) for r in surf]
        cf = [float(r["cf"]) for r in surf]
        order = np.argsort(th)
        th = np.array(th)[order]
        ax.plot(th, np.array(cp)[order], "-", lw=1, label=r"$C_p$")
        ax.plot(th, np.array(cf)[order], "--", lw=1, label=r"$C_f$")
        ax.set_xlabel(r"angle $\theta$ [deg]")
        ax.legend()
        ax.set_title(f"{case}: cylinder wall pressure/friction")
        ylab = "cp,cf"
    ax.grid(True, alpha=0.3)
    fname = f"{case}_surface.png"
    fig.tight_layout()
    fig.savefig(os.path.join(FIGDIR, fname), dpi=150)
    plt.close(fig)
    add_fig(fname, case, "surface", ylab, f"results/{case}/surface.csv",
            f"{case}: wall {ylab} distribution")


def plot_fields(case):
    p = field_path(case)
    if not p:
        return
    x, y, cells, arrays = read_vtu(p)
    near = case.startswith("cylinder")
    xmin, xmax = (-2.0, 4.0) if near else (-1.5, 2.5)
    ymin, ymax = (-2.0, 2.0) if near else (-1.0, 1.0)
    for var, cmap, label in [
        ("MachNumber", "jet", "Mach number"),
        ("Pressure", "jet", "Pressure"),
    ]:
        if var not in arrays:
            continue
        tri, z = cell_triangulation(x, y, cells, arrays[var])
        fig, ax = plt.subplots(figsize=(9, 5.2))
        # Percentile-clipped range: a few outlier cells must not collapse the
        # color scale of the whole figure (contract requirement).
        zlo, zhi = np.nanpercentile(z, 1.0), np.nanpercentile(z, 99.0)
        if zhi - zlo < 1e-12:
            zlo, zhi = np.nanmin(z), np.nanmax(z)
        levels = np.linspace(zlo, zhi, 40)
        cf = ax.tricontourf(tri, z, levels=levels, cmap=cmap)
        cb = fig.colorbar(cf, ax=ax)
        cb.set_label(label + (f"  [{zlo:.4g}, {zhi:.4g}]" if var == "Pressure" else ""))
        ax.set_aspect("equal")
        ax.set_xlim(xmin, xmax)
        ax.set_ylim(ymin, ymax)
        ax.set_xlabel("x")
        ax.set_ylabel("y")
        ax.set_title(f"{case}: {label}")
        vname = "mach" if var == "MachNumber" else "pressure"
        fname = f"{case}_field_{vname}.png"
        fig.tight_layout()
        fig.savefig(os.path.join(FIGDIR, fname), dpi=150)
        plt.close(fig)
        add_fig(fname, case, "field", vname, f"results/{case}/field_final.vtu",
                f"{case}: {label} field")


def plot_velocity_magnitude(case):
    p = field_path(case)
    if not p or case != "cylinder_m010_laminar_re200":
        return
    x, y, cells, arrays = read_vtu(p)
    u = arrays.get("VelocityX")
    v = arrays.get("VelocityY")
    if u is None or v is None:
        return
    vm = np.sqrt(u * u + v * v)
    tri, z = cell_triangulation(x, y, cells, vm)
    fig, ax = plt.subplots(figsize=(9, 5.2))
    zlo, zhi = np.nanpercentile(z, 1.0), np.nanpercentile(z, 99.0)
    cf = ax.tricontourf(tri, z, levels=np.linspace(zlo, zhi, 40), cmap="jet")
    cb = fig.colorbar(cf, ax=ax)
    cb.set_label("velocity magnitude")
    ax.set_aspect("equal")
    ax.set_xlim(-1.5, 6.0)
    ax.set_ylim(-2.0, 2.0)
    ax.set_xlabel("x")
    ax.set_ylabel("y")
    ax.set_title("cylinder Re200: wake velocity magnitude (vortex street)")
    fname = "cylinder_m010_laminar_re200_velocity.png"
    fig.tight_layout()
    fig.savefig(os.path.join(FIGDIR, fname), dpi=150)
    plt.close(fig)
    add_fig(fname, case, "field", "velocity_magnitude",
            f"results/{case}/field_final.vtu",
            "cylinder Re200: post-transient wake velocity magnitude")


def plot_vorticity(case):
    """Vorticity estimate for the Re200 wake from the linear-triangle
    velocity gradients (documented clipped range [-5,5] as recommended)."""
    p = field_path(case)
    if not p or case != "cylinder_m010_laminar_re200":
        return
    x, y, cells, arrays = read_vtu(p)
    u = arrays.get("VelocityX")
    v = arrays.get("VelocityY")
    if u is None or v is None:
        return
    cx = np.array([np.mean(x[c]) for c in cells])
    cy = np.array([np.mean(y[c]) for c in cells])
    # Delaunay triangulation of the cell centers; the piecewise-linear
    # velocity interpolant has constant gradients per triangle.
    dtri = mtri.Triangulation(cx, cy)
    from matplotlib.tri import LinearTriInterpolator
    gu = LinearTriInterpolator(dtri, u).gradient(cx, cy)
    gv = LinearTriInterpolator(dtri, v).gradient(cx, cy)
    # vorticity (curl z) at each cell center; flat-shade per cell
    vort = np.array(gv[0]) - np.array(gu[1])
    vort = np.where(np.isfinite(vort), vort, 0.0)
    tri, zz = cell_triangulation(x, y, cells, vort)
    fig, ax = plt.subplots(figsize=(9, 5.2))
    zlo, zhi = -5.0, 5.0
    cf = ax.tricontourf(tri, zz, levels=np.linspace(zlo, zhi, 41),
                        cmap="RdBu_r", extend="both")
    cb = fig.colorbar(cf, ax=ax)
    cb.set_label("vorticity  (clipped [-5, 5])")
    ax.set_aspect("equal")
    ax.set_xlim(-1.5, 6.0)
    ax.set_ylim(-2.0, 2.0)
    ax.set_xlabel("x")
    ax.set_ylabel("y")
    ax.set_title("cylinder Re200: wake vorticity (vortex street)")
    fname = "cylinder_m010_laminar_re200_vorticity.png"
    fig.tight_layout()
    fig.savefig(os.path.join(FIGDIR, fname), dpi=150)
    plt.close(fig)
    add_fig(fname, case, "field", "vorticity",
            f"results/{case}/field_final.vtu",
            "cylinder Re200: post-transient wake vorticity, clipped [-5,5]")


def main():
    for case in CASES:
        print(f"figures for {case}")
        plot_history(case)
        plot_surface(case)
        plot_fields(case)
    plot_velocity_magnitude("cylinder_m010_laminar_re200")
    plot_vorticity("cylinder_m010_laminar_re200")
    with open(os.path.join(ROOT, "report", "figure_manifest.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(manifest[0].keys()))
        w.writeheader()
        w.writerows(manifest)
    print(f"wrote {len(manifest)} figure manifest entries")


if __name__ == "__main__":
    main()
