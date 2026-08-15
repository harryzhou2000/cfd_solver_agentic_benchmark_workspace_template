#!/usr/bin/env python3
"""Plotting utilities for fv2d solver outputs.

Generates publication-style figures from case output directories:
residual histories, force histories, surface cp, and field contours
(Mach, pressure, vorticity) rendered from the unstructured VTU cells.
"""
from __future__ import annotations

import argparse
import csv
import json
import re
from pathlib import Path

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.tri as mtri

plt.rcParams.update({
    "font.size": 11,
    "axes.grid": True,
    "grid.alpha": 0.3,
    "lines.linewidth": 1.4,
    "figure.dpi": 130,
    "savefig.bbox": "tight",
})


def read_csv(path):
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def col(rows, name):
    return np.array([float(r[name]) for r in rows])


def read_vtu(path):
    txt = open(path).read()

    def arr(name, dt=float):
        m = re.search(r'Name="%s"[^>]*>\s*(.*?)</DataArray>' % name, txt, re.S)
        return np.fromstring(m.group(1), sep=" ").astype(dt)

    pts = re.search(r"<Points>\s*<DataArray[^>]*>\s*(.*?)</DataArray>", txt, re.S).group(1)
    P = np.fromstring(pts, sep=" ").reshape(-1, 3)
    C = arr("connectivity", np.int64)
    offs = arr("offsets", np.int64)
    types = arr("types", np.int64)
    data = {}
    for m in re.finditer(r'<DataArray type="Float64" Name="([^"]+)" format="ascii">\s*(.*?)</DataArray>', txt, re.S):
        data[m.group(1)] = np.fromstring(m.group(2), sep=" ")
    # split quads into triangles, tracking cell index per triangle
    tris, cellidx = [], []
    prev = 0
    for i, (o, t) in enumerate(zip(offs, types)):
        nd = C[prev:o]
        prev = o
        if t == 5:
            tris.append(nd)
            cellidx.append(i)
        else:
            tris.append(nd[[0, 1, 2]])
            tris.append(nd[[0, 2, 3]])
            cellidx += [i, i]
    tri = mtri.Triangulation(P[:, 0], P[:, 1], np.array(tris))
    return tri, np.array(cellidx), data


def plot_residuals(case_dir, out_png):
    rows = read_csv(case_dir / "residuals.csv")
    step = col(rows, "step")
    fig, ax = plt.subplots(figsize=(7, 5))
    ax.semilogy(step, col(rows, "rho"), label=r"$\rho$")
    ax.semilogy(step, col(rows, "rhou"), label=r"$\rho u$")
    ax.semilogy(step, col(rows, "rhov"), label=r"$\rho v$")
    ax.semilogy(step, col(rows, "rhoE"), label=r"$\rho E$")
    ax.semilogy(step, col(rows, "residual_l2"), "k--", lw=1.8, label="combined $L_2$")
    ax.set_xlabel("nonlinear step" if col(rows, "physical_time").max() == 0 else "physical step")
    ax.set_ylabel("residual RMS")
    ax.set_title(f"Residual history — {case_dir.name}")
    ax.legend()
    fig.savefig(out_png)
    plt.close(fig)


def plot_forces(case_dir, out_png):
    rows = read_csv(case_dir / "forces.csv")
    x = col(rows, "physical_time")
    if x.max() == 0:
        x = col(rows, "step")
        xlabel = "nonlinear step"
    else:
        xlabel = "physical time"
    fig, ax = plt.subplots(figsize=(7.5, 5))
    cl, cd, cmz = col(rows, "cl"), col(rows, "cd"), col(rows, "cmz")
    ax.plot(x, cl, label="$C_l$")
    ax.plot(x, cd, label="$C_d$")
    ax.plot(x, cmz, label="$C_{m,z}$")
    # clip y-limits to the post-startup range so a startup spike does not
    # compress the settled oscillation
    n0 = max(1, len(x) // 20)
    tail = np.concatenate([cl[n0:], cd[n0:], cmz[n0:]])
    lo, hi = np.percentile(tail, [0.5, 99.5])
    pad = 0.15 * max(hi - lo, 1e-6)
    ax.set_ylim(lo - pad, hi + pad)
    ax.set_xlabel(xlabel)
    ax.set_ylabel("force/moment coefficient")
    ax.set_title(f"Force history — {case_dir.name}")
    ax.legend()
    fig.savefig(out_png)
    plt.close(fig)


def plot_surface_cp(case_dir, out_png, title=None):
    rows = read_csv(case_dir / "surface.csv")
    x = col(rows, "x")
    y = col(rows, "y")
    cp = col(rows, "cp")
    cf = col(rows, "cf")
    viscous = np.max(np.abs(cf)) > 1e-12
    up = y >= 0
    iu = np.argsort(x[up])
    il = np.argsort(x[~up])
    if viscous:
        fig, (ax, ax2) = plt.subplots(1, 2, figsize=(13, 4.8))
    else:
        fig, ax = plt.subplots(figsize=(7, 5))
    ax.plot(x[up][iu], cp[up][iu], ".", ms=2.5, label="upper")
    ax.plot(x[~up][il], cp[~up][il], ".", ms=2.5, label="lower")
    ax.invert_yaxis()
    ax.set_xlabel("x/c")
    ax.set_ylabel("$C_p$")
    ax.set_title((title or f"Surface pressure coefficient — {case_dir.name}"))
    ax.legend()
    if viscous:
        ax2.plot(x[up][iu], cf[up][iu], ".", ms=2.5, label="upper")
        ax2.plot(x[~up][il], cf[~up][il], ".", ms=2.5, label="lower")
        ax2.set_xlabel("x/c")
        ax2.set_ylabel("$C_f$ (tangential)")
        ax2.set_title(f"Skin friction — {case_dir.name}")
        ax2.legend()
    fig.savefig(out_png)
    plt.close(fig)


def plot_surface_cp_cylinder(case_dir, out_png):
    rows = read_csv(case_dir / "surface.csv")
    x = col(rows, "x")
    y = col(rows, "y")
    cp = col(rows, "cp")
    cf = col(rows, "cf")
    theta = np.degrees(np.arctan2(y, x))
    order = np.argsort(theta)
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 4.6))
    ax1.plot(theta[order], cp[order], ".", ms=2.5)
    ax1.set_xlabel(r"$\theta$ [deg]")
    ax1.set_ylabel("$C_p$")
    ax1.set_title(f"Cylinder wall $C_p$ — {case_dir.name}")
    ax2.plot(theta[order], cf[order], ".", ms=2.5)
    ax2.set_xlabel(r"$\theta$ [deg]")
    ax2.set_ylabel("$C_f$")
    ax2.set_title(f"Cylinder wall skin friction — {case_dir.name}")
    fig.savefig(out_png)
    plt.close(fig)


def plot_field(case_dir, out_png, variable, label, window=None, clip=None, cmap="viridis",
               vtu_name="field_final.vtu"):
    tri, cellidx, data = read_vtu(case_dir / vtu_name)
    if variable == "velocity_magnitude":
        data = dict(data)
        data["velocity_magnitude"] = np.hypot(data["velocity_u"], data["velocity_v"])
    if variable not in data:
        raise KeyError(f"variable {variable} not in {vtu_name}: {sorted(data)}")
    vals = data[variable][cellidx]
    vmin = clip[0] if clip else None
    vmax = clip[1] if clip else None
    fig, ax = plt.subplots(figsize=(8, 5.6))
    h = ax.tripcolor(tri, vals, shading="flat", cmap=cmap, vmin=vmin, vmax=vmax)
    cb = fig.colorbar(h, ax=ax)
    cb.set_label(label)
    ax.set_xlabel("x")
    ax.set_ylabel("y")
    ax.set_aspect("equal")
    if window:
        ax.set_xlim(window[0], window[1])
        ax.set_ylim(window[2], window[3])
    ax.set_title(f"{label} — {case_dir.name}")
    ax.grid(False)
    fig.savefig(out_png)
    plt.close(fig)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("case_dir", type=Path)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--kind", required=True,
                    choices=["residuals", "forces", "surface_cp", "surface_cp_cyl",
                             "mach", "pressure", "vorticity", "velocity_magnitude"])
    ap.add_argument("--window", type=float, nargs=4, default=None)
    ap.add_argument("--clip", type=float, nargs=2, default=None)
    ap.add_argument("--vtu", default="field_final.vtu")
    args = ap.parse_args()

    args.out.parent.mkdir(parents=True, exist_ok=True)
    if args.kind == "residuals":
        plot_residuals(args.case_dir, args.out)
    elif args.kind == "forces":
        plot_forces(args.case_dir, args.out)
    elif args.kind == "surface_cp":
        plot_surface_cp(args.case_dir, args.out)
    elif args.kind == "surface_cp_cyl":
        plot_surface_cp_cylinder(args.case_dir, args.out)
    elif args.kind in ("mach", "pressure", "vorticity"):
        labels = {"mach": "Mach number", "pressure": "pressure", "vorticity": "vorticity"}
        plot_field(args.case_dir, args.out, args.kind, labels[args.kind],
                   window=args.window, clip=args.clip, vtu_name=args.vtu)
    elif args.kind == "velocity_magnitude":
        tri, cellidx, data = read_vtu(args.case_dir / args.vtu)
        mag = np.hypot(data["velocity_u"], data["velocity_v"])[cellidx]
        vmin = args.clip[0] if args.clip else None
        vmax = args.clip[1] if args.clip else None
        fig, ax = plt.subplots(figsize=(8, 5.6))
        h = ax.tripcolor(tri, mag, shading="flat", cmap="viridis", vmin=vmin, vmax=vmax)
        cb = fig.colorbar(h, ax=ax)
        cb.set_label("velocity magnitude")
        ax.set_xlabel("x"); ax.set_ylabel("y"); ax.set_aspect("equal")
        if args.window:
            ax.set_xlim(args.window[0], args.window[1])
            ax.set_ylim(args.window[2], args.window[3])
        ax.set_title(f"velocity magnitude — {args.case_dir.name}")
        ax.grid(False)
        fig.savefig(args.out)
        plt.close(fig)


if __name__ == "__main__":
    main()
