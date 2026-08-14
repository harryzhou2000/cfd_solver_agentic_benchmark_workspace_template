#!/usr/bin/env python3
"""Generate all report figures + figure_manifest.csv from solver result dirs.

Usage: .venv/bin/python tools/plot_results.py --results results --figures report/figures \
          --manifest report/figure_manifest.csv [--cases a,b,c]
"""
import argparse
import csv
import json
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.colors
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.tri import Triangulation

sys.path.insert(0, str(Path(__file__).parent))
from vtu_dump import read_vtu

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


def load_vtu_mesh(path):
    f = read_vtu(str(path))
    pts = f["Points"]
    conn = f["connectivity"]
    off = f["offsets"]
    tris = []
    start = 0
    cell_of_tri = []
    for c in range(len(off)):
        ids = conn[start : off[c]]
        start = off[c]
        if len(ids) == 3:
            tris.append(ids)
            cell_of_tri.append(c)
        else:
            tris.append([ids[0], ids[1], ids[2]])
            tris.append([ids[0], ids[2], ids[3]])
            cell_of_tri.extend([c, c])
    tri = Triangulation(pts[:, 0], pts[:, 1], np.array(tris))
    return f, tri, np.array(cell_of_tri)


def cell_field_to_tri(values, cell_of_tri):
    return values[cell_of_tri]


def plot_field(tri, cellvals, cell_of_tri, varname, out, window=None, clip=None, cmap="jet",
               levels=64, label=None):
    z = cell_field_to_tri(cellvals, cell_of_tri)
    if clip is not None:
        z = np.clip(z, clip[0], clip[1])
    fig, ax = plt.subplots(figsize=(8, 5))
    if clip is not None:
        norm = matplotlib.colors.Normalize(clip[0], clip[1])
        cs = ax.tripcolor(tri, facecolors=z, cmap=cmap, norm=norm)
    else:
        cs = ax.tripcolor(tri, facecolors=z, cmap=cmap, shading="flat")
    cb = fig.colorbar(cs, ax=ax)
    cb.set_label(label or varname)
    ax.set_xlabel("x")
    ax.set_ylabel("y")
    ax.set_aspect("equal")
    if window:
        ax.set_xlim(window[0], window[1])
        ax.set_ylim(window[2], window[3])
    ax.set_title(varname)
    fig.savefig(out)
    plt.close(fig)


def read_csv(path):
    with open(path) as fh:
        return list(csv.DictReader(fh))


def col(rows, name, dtype=float):
    return np.array([dtype(r[name]) for r in rows])


def plot_residuals(case_id, rows, out):
    step = col(rows, "step", int)
    fig, ax = plt.subplots(figsize=(7, 4.5))
    ax.semilogy(step, col(rows, "residual_l2"), label="L2 (RMS)")
    ax.semilogy(step, col(rows, "residual_linf"), label="Linf", alpha=0.7)
    for comp in ["rho", "rhou", "rhov", "rhoE"]:
        ax.semilogy(step, col(rows, comp), label=comp, alpha=0.5)
    ax.set_xlabel("pseudo-time step" if col(rows, "physical_time").max() == 0 else "physical step")
    ax.set_ylabel("residual (RMS of R/Vol)")
    ax.set_title(f"Residual history — {case_id}")
    ax.legend(ncol=2, fontsize=9)
    fig.savefig(out)
    plt.close(fig)


def plot_forces(case_id, rows, out):
    step = col(rows, "step", int)
    fig, ax = plt.subplots(figsize=(7, 4.5))
    ax.plot(step, col(rows, "cl"), label="$c_l$")
    ax.plot(step, col(rows, "cd"), label="$c_d$")
    ax.plot(step, col(rows, "cmz"), label="$c_{m,z}$", alpha=0.7)
    ax.set_xlabel("step")
    ax.set_ylabel("force coefficient")
    ax.set_title(f"Force history — {case_id}")
    ax.legend()
    fig.savefig(out)
    plt.close(fig)


def plot_surface_cp(case_id, rows, out, cylinder=False):
    x = col(rows, "x")
    y = col(rows, "y")
    cp = col(rows, "cp")
    fig, ax = plt.subplots(figsize=(7, 4.5))
    if cylinder:
        theta = np.degrees(np.arctan2(y, x))
        order = np.argsort(theta)
        ax.plot(theta[order], cp[order], ".", ms=3)
        ax.set_xlabel("azimuth [deg]")
    else:
        top = y >= 0
        o1 = np.argsort(x[top])
        o2 = np.argsort(x[~top])
        ax.plot(x[top][o1], cp[top][o1], ".", ms=3, label="upper")
        ax.plot(x[~top][o2], cp[~top][o2], ".", ms=3, label="lower")
        ax.set_xlabel("x/c")
        ax.legend()
    ax.invert_yaxis()
    ax.set_ylabel("$c_p$")
    ax.set_title(f"Surface pressure coefficient — {case_id}")
    fig.savefig(out)
    plt.close(fig)


def plot_surface_cf(case_id, rows, out):
    x = col(rows, "x")
    y = col(rows, "y")
    cf = col(rows, "cf")
    fig, ax = plt.subplots(figsize=(7, 4.5))
    theta = np.degrees(np.arctan2(y, x))
    order = np.argsort(theta)
    ax.plot(theta[order], cf[order], ".", ms=3)
    ax.set_xlabel("azimuth [deg]")
    ax.set_ylabel("$c_f$")
    ax.set_title(f"Wall skin friction — {case_id}")
    fig.savefig(out)
    plt.close(fig)


def vorticity_field(f, tri_conn, cell_of_tri):
    """Cell-centered vorticity via Green-Gauss with face values averaged from
    the two adjacent cells (boundary faces use the owning cell)."""
    pts = f["Points"]
    conn = f["connectivity"]
    off = f["offsets"]
    u = f["velocity_u"]
    v = f["velocity_v"]
    ncell = len(off)
    # edge -> cells
    from collections import defaultdict

    edge_cells = defaultdict(list)
    start = 0
    cell_edges = []
    for c in range(ncell):
        ids = conn[start : off[c]]
        start = off[c]
        n = len(ids)
        es = []
        for k in range(n):
            a, b = int(ids[k]), int(ids[(k + 1) % n])
            key = (min(a, b), max(a, b))
            edge_cells[key].append(c)
            es.append((a, b, key))
        cell_edges.append(es)
    omega = np.zeros(ncell)
    for c in range(ncell):
        ids_slice = cell_edges[c]
        vol2 = 0.0
        acc = 0.0
        x = pts[[e[0] for e in ids_slice], 0]
        y = pts[[e[0] for e in ids_slice], 1]
        x2 = pts[[e[1] for e in ids_slice], 0]
        y2 = pts[[e[1] for e in ids_slice], 1]
        dx = x2 - x
        dy = y2 - y
        for k, (a, b, key) in enumerate(ids_slice):
            cs = edge_cells[key]
            if len(cs) == 2:
                j = cs[0] if cs[1] == c else cs[1]
                uf = 0.5 * (u[c] + u[j])
                vf = 0.5 * (v[c] + v[j])
            else:
                uf = u[c]
                vf = v[c]
            acc += vf * dy[k] + uf * dx[k]
        # signed volume: dividing by it makes the result orientation-invariant
        xx = np.append(x, x[0])
        yy = np.append(y, y[0])
        vol = 0.5 * np.sum(xx[:-1] * yy[1:] - xx[1:] * yy[:-1])
        omega[c] = acc / vol
    return omega


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--results", required=True)
    ap.add_argument("--figures", required=True)
    ap.add_argument("--manifest", required=True)
    ap.add_argument("--cases", default="")
    args = ap.parse_args()

    results = Path(args.results)
    figdir = Path(args.figures)
    figdir.mkdir(parents=True, exist_ok=True)
    case_dirs = sorted(p for p in results.iterdir() if p.is_dir() and (p / "metadata.json").exists())
    if args.cases:
        keep = set(args.cases.split(","))
        case_dirs = [p for p in case_dirs if p.name in keep]

    manifest = []
    for cd in case_dirs:
        cid = cd.name
        meta = json.loads((cd / "metadata.json").read_text())
        transient = meta.get("time_integrator", "").startswith("bdf2")

        # line plots
        res = read_csv(cd / "residuals.csv")
        plot_residuals(cid, res, figdir / f"{cid}_residuals.png")
        manifest.append((f"{cid}_residuals.png", cid, "line", "residual_l2",
                         str(cd / "residuals.csv"), f"Residual history for {cid}"))
        forces = read_csv(cd / "forces.csv")
        plot_forces(cid, forces, figdir / f"{cid}_forces.png")
        manifest.append((f"{cid}_forces.png", cid, "line", "cl_cd_cmz",
                         str(cd / "forces.csv"), f"Force history for {cid}"))
        surf = read_csv(cd / "surface.csv")
        if "naca" in cid:
            plot_surface_cp(cid, surf, figdir / f"{cid}_surface_cp.png")
            manifest.append((f"{cid}_surface_cp.png", cid, "line", "cp",
                             str(cd / "surface.csv"), f"NACA surface $c_p$ for {cid}"))
        else:
            plot_surface_cp(cid, surf, figdir / f"{cid}_surface_cp.png", cylinder=True)
            manifest.append((f"{cid}_surface_cp.png", cid, "line", "cp",
                             str(cd / "surface.csv"), f"Cylinder wall $c_p$ for {cid}"))
            plot_surface_cf(cid, surf, figdir / f"{cid}_surface_cf.png")
            manifest.append((f"{cid}_surface_cf.png", cid, "line", "cf",
                             str(cd / "surface.csv"), f"Cylinder wall $c_f$ for {cid}"))

        # field plots
        f, tri, cell_of_tri = load_vtu_mesh(cd / "field_final.vtu")
        pts = f["Points"]
        xmin, xmax = pts[:, 0].min(), pts[:, 0].max()
        ymin, ymax = pts[:, 1].min(), pts[:, 1].max()
        if "naca" in cid:
            zoom = (-0.5, 1.6, -0.6, 0.6)
        else:
            zoom = (-2.0, 6.0, -3.0, 3.0)
        pmin, pmax = float(f["pressure"].min()), float(f["pressure"].max())
        mmax = float(f["mach"].max())
        for var, arr, clip in [
            ("mach", f["mach"], (0.0, max(0.01, np.percentile(f["mach"], 99)))),
            ("pressure", f["pressure"], None),
        ]:
            plot_field(tri, arr, cell_of_tri, var, figdir / f"{cid}_{var}.png",
                       window=(xmin, xmax, ymin, ymax), clip=clip,
                       label=var)
            manifest.append((f"{cid}_{var}.png", cid, "contour", var,
                             str(cd / "field_final.vtu"), f"{var} field for {cid} (full domain)"))
            plot_field(tri, arr, cell_of_tri, var, figdir / f"{cid}_{var}_zoom.png",
                       window=zoom, clip=clip, label=var)
            manifest.append((f"{cid}_{var}_zoom.png", cid, "contour", var,
                             str(cd / "field_final.vtu"), f"{var} field for {cid} (near body)"))

        if "re200" in cid:
            omega = vorticity_field(f, None, cell_of_tri)
            plot_field(tri, omega, cell_of_tri, "vorticity",
                       figdir / f"{cid}_vorticity_wake.png",
                       window=(-1.5, 10.0, -3.0, 3.0), clip=(-5.0, 5.0), cmap="RdBu_r",
                       label="vorticity (clipped [-5,5])")
            manifest.append((f"{cid}_vorticity_wake.png", cid, "contour", "vorticity",
                             str(cd / "field_final.vtu"),
                             "Post-transient vortex street vorticity (clipped [-5,5])"))
            vmag = np.sqrt(f["velocity_u"] ** 2 + f["velocity_v"] ** 2)
            plot_field(tri, vmag, cell_of_tri, "velocity_magnitude",
                       figdir / f"{cid}_velocity_wake.png", window=(-1.5, 10.0, -3.0, 3.0),
                       label="|u|")
            manifest.append((f"{cid}_velocity_wake.png", cid, "contour", "velocity_magnitude",
                             str(cd / "field_final.vtu"), "Wake velocity magnitude"))

    with open(args.manifest, "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["figure_file", "case_id", "figure_type", "variable", "source_file", "caption"])
        w.writerows(manifest)
    print(f"wrote {len(manifest)} figures + manifest {args.manifest}")


if __name__ == "__main__":
    main()
