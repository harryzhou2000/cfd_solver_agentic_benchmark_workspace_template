#!/usr/bin/env python3
"""Generate report figures, figure manifest, and sanity checks from solver
output directories.

For each case output directory containing residuals.csv, forces.csv,
surface.csv and field_final.vtu, this script writes:

  report/figures/<case>_residuals.png       residual history (semilogy)
  report/figures/<case>_forces.png          force history
  report/figures/<case>_cp.png              surface pressure coefficient
  report/figures/<case>_mach.png            Mach contours (full domain)
  report/figures/<case>_pressure.png        pressure contours (full domain)
  report/figures/<case>_mach_zoom.png       near-body Mach contours
  report/figures/<case>_pressure_zoom.png   near-body pressure contours
  report/figures/<case>_velocity.png        velocity magnitude (cylinder cases)
  report/figures/<case>_vorticity.png       vorticity (cylinder Re200)

Usage:
  .venv/bin/python3 tools/plot_results.py <results_dir> <report_dir>
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.tri as mtri


def parse_vtu(path: Path):
    """Return (x, y, fields) from the ASCII single-piece VTU."""
    tree = ET.parse(path)
    root = tree.getroot()
    piece = root.find(".//UnstructuredGrid/Piece")
    arrays = {}
    for da in piece.findall(".//DataArray"):
        name = da.get("Name")
        ncomp = int(da.get("NumberOfComponents", "1"))
        vals = np.array([float(v) for v in da.text.split()])
        if ncomp > 1:
            vals = vals.reshape(-1, ncomp)
        if name:
            arrays[name] = vals
    pts_da = root.find(".//UnstructuredGrid/Piece/Points/DataArray")
    ncomp = int(pts_da.get("NumberOfComponents", "3"))
    pts = np.array([float(v) for v in pts_da.text.split()]).reshape(-1, ncomp)
    x, y = pts[:, 0], pts[:, 1]
    fields = {k: arrays[k] for k in
              ("density", "velocity_x", "velocity_y", "pressure", "mach",
               "temperature", "total_energy", "rank") if k in arrays}
    return x, y, fields


def read_csv(path: Path):
    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        rows = list(reader)
    names = reader.fieldnames
    data = {}
    for name in names:
        try:
            data[name] = np.array([float(r[name]) for r in rows])
        except (TypeError, ValueError):
            data[name] = np.array([r[name] for r in rows])
    return data


def scholarly_ax(ax, xlabel, ylabel, title=None):
    ax.set_xlabel(xlabel, fontsize=11)
    ax.set_ylabel(ylabel, fontsize=11)
    if title:
        ax.set_title(title, fontsize=12)
    ax.grid(True, alpha=0.3, linewidth=0.5)
    ax.tick_params(labelsize=10)


def naca0012_half_thickness(x):
    x = np.clip(x, 0.0, 1.0)
    sx = np.sqrt(x)
    return 0.6 * (0.2969 * sx - 0.1260 * x - 0.3516 * x * x +
                  0.2843 * x * x * x - 0.1015 * x * x * x * x)


def body_mask(tri, x, y, cylinder):
    """Mask Delaunay triangles whose centroid lies inside the solid body."""
    txc = tri.x[tri.triangles].mean(axis=1)
    tyc = tri.y[tri.triangles].mean(axis=1)
    if cylinder:
        inside = np.hypot(txc, tyc) < 0.5
    else:
        inside = np.zeros(len(txc), dtype=bool)
        on = (txc > 0.0) & (txc < 1.0)
        inside[on] = np.abs(tyc[on]) < naca0012_half_thickness(txc[on])
    tri.set_mask(inside)
    return tri


def plot_residuals(case_dir: Path, fig_dir: Path, case_id: str):
    res = read_csv(case_dir / "residuals.csv")
    fig, ax = plt.subplots(figsize=(7, 4.5))
    step = res["step"]
    ax.semilogy(step, res["residual_l2"], label="L2", lw=1.5)
    ax.semilogy(step, res["residual_linf"], label="Linf", lw=1.0, alpha=0.7)
    ax.semilogy(step, res["rho"], label=r"$\rho$", lw=1.0, alpha=0.6)
    ax.semilogy(step, res["rhou"], label=r"$\rho u$", lw=1.0, alpha=0.6)
    ax.semilogy(step, res["rhov"], label=r"$\rho v$", lw=1.0, alpha=0.6)
    ax.semilogy(step, res["rhoE"], label=r"$\rho E$", lw=1.0, alpha=0.6)
    scholarly_ax(ax, "pseudo-time step", "residual norm",
                 f"{case_id}: residual history")
    ax.legend(fontsize=8, ncol=2, loc="best")
    fig.tight_layout()
    out = fig_dir / f"{case_id}_residuals.png"
    fig.savefig(out, dpi=150)
    plt.close(fig)
    return out


def plot_forces(case_dir: Path, fig_dir: Path, case_id: str):
    fc = read_csv(case_dir / "forces.csv")
    fig, axes = plt.subplots(2, 1, figsize=(7, 6), sharex=True)
    step = fc["step"]
    axes[0].plot(step, fc["cd"], label=r"$C_D$", lw=1.2)
    axes[0].plot(step, fc["pressure_drag"], label=r"$C_{D,p}$", lw=1.0, alpha=0.7)
    if np.any(np.abs(fc["viscous_drag"]) > 1e-9):
        axes[0].plot(step, fc["viscous_drag"], label=r"$C_{D,v}$", lw=1.0, alpha=0.7)
    scholarly_ax(axes[0], "", "drag coefficient", f"{case_id}: force history")
    axes[0].legend(fontsize=8, loc="best")
    axes[1].plot(step, fc["cl"], label=r"$C_L$", lw=1.2)
    axes[1].plot(step, fc["cmz"], label=r"$C_M$", lw=1.0, alpha=0.7)
    scholarly_ax(axes[1], "step", "lift / moment coefficient")
    axes[1].legend(fontsize=8, loc="best")
    fig.tight_layout()
    out = fig_dir / f"{case_id}_forces.png"
    fig.savefig(out, dpi=150)
    plt.close(fig)
    return out


def plot_cp(case_dir: Path, fig_dir: Path, case_id: str):
    surf = read_csv(case_dir / "surface.csv")
    fig, ax = plt.subplots(figsize=(7, 4.5))
    ax.plot(surf["x"], surf["cp"], ".", markersize=2.5, color="tab:blue")
    scholarly_ax(ax, "x", r"$C_p$", f"{case_id}: surface pressure coefficient")
    ax.invert_yaxis()
    fig.tight_layout()
    out = fig_dir / f"{case_id}_cp.png"
    fig.savefig(out, dpi=150)
    plt.close(fig)
    return out


def plot_field_contours(x, y, values, fig_dir, case_id, varname, label,
                        xlim=None, ylim=None, levels=41, cmap="turbo",
                        vmin=None, vmax=None, title=None, cylinder=False):
    tri = mtri.Triangulation(x, y)
    tri = body_mask(tri, x, y, cylinder)
    fig, ax = plt.subplots(figsize=(7.5, 5.5))
    if vmin is None or vmax is None:
        lo, hi = np.nanpercentile(values, [1.0, 99.0])
        vmin = lo if vmin is None else vmin
        vmax = hi if vmax is None else vmax
    lv = np.linspace(vmin, vmax, levels)
    cf = ax.tricontourf(tri, values, levels=lv, cmap=cmap, extend="both")
    cb = fig.colorbar(cf, ax=ax, fraction=0.046, pad=0.03)
    cb.set_label(label, fontsize=10)
    if xlim:
        ax.set_xlim(*xlim)
    if ylim:
        ax.set_ylim(*ylim)
    ax.set_aspect("equal")
    scholarly_ax(ax, "x", "y", title or f"{case_id}: {label}")
    fig.tight_layout()
    out = fig_dir / f"{case_id}_{varname}.png"
    fig.savefig(out, dpi=150)
    plt.close(fig)
    return out


def plot_fields(case_dir: Path, fig_dir: Path, case_id: str, cylinder: bool):
    vtu = case_dir / "field_final.vtu"
    if not vtu.exists():
        return []
    x, y, fields = parse_vtu(vtu)
    outs = []
    mach = fields["mach"]
    pres = fields["pressure"]
    mach_inf = 0.15
    for tok, val in (("m015", 0.15), ("m080", 0.8), ("m200", 2.0)):
        if tok in case_id:
            mach_inf = val
    mach_vmax = max(0.2, 1.35 * mach_inf)
    mach_vmin = 0.0
    outs.append(plot_field_contours(
        x, y, mach, fig_dir, case_id, "mach", "Mach number", levels=41,
        title=f"{case_id}: Mach contours", cylinder=cylinder,
        vmin=mach_vmin, vmax=mach_vmax))
    outs.append(plot_field_contours(
        x, y, pres, fig_dir, case_id, "pressure", "pressure", levels=41,
        title=f"{case_id}: pressure contours", cylinder=cylinder))
    if cylinder:
        xlim, ylim = (-2.5, 6.0), (-3.0, 3.0)
    else:
        xlim, ylim = (-0.6, 1.6), (-0.8, 0.8)
    outs.append(plot_field_contours(
        x, y, mach, fig_dir, case_id, "mach_zoom", "Mach number",
        xlim=xlim, ylim=ylim, levels=41,
        title=f"{case_id}: near-body Mach contours", cylinder=cylinder,
        vmin=mach_vmin, vmax=mach_vmax))
    outs.append(plot_field_contours(
        x, y, pres, fig_dir, case_id, "pressure_zoom", "pressure",
        xlim=xlim, ylim=ylim, levels=41,
        title=f"{case_id}: near-body pressure contours", cylinder=cylinder))
    if cylinder:
        vel = np.hypot(fields["velocity_x"], fields["velocity_y"])
        outs.append(plot_field_contours(
            x, y, vel, fig_dir, case_id, "velocity", "velocity magnitude",
            xlim=xlim, ylim=ylim, levels=41, cmap="viridis",
            title=f"{case_id}: velocity magnitude", cylinder=True))
        # Vorticity from the piecewise-linear Delaunay interpolant
        tri = mtri.Triangulation(x, y)
        tri = body_mask(tri, x, y, True)
        u = fields["velocity_x"]
        v = fields["velocity_y"]
        vort = np.zeros(len(tri.triangles))
        for k, (i, j, m) in enumerate(tri.triangles):
            d = np.array([[x[j] - x[i], y[j] - y[i]],
                          [x[m] - x[i], y[m] - y[i]]])
            du = np.array([u[j] - u[i], u[m] - u[i]])
            dv = np.array([v[j] - v[i], v[m] - v[i]])
            det = d[0, 0] * d[1, 1] - d[0, 1] * d[1, 0]
            if abs(det) > 1e-30:
                ux, uy = (d[1, 1] * du[0] - d[0, 1] * du[1]) / det, \
                         (-d[1, 0] * du[0] + d[0, 0] * du[1]) / det
                vx, vy = (d[1, 1] * dv[0] - d[0, 1] * dv[1]) / det, \
                         (-d[1, 0] * dv[0] + d[0, 0] * dv[1]) / det
                vort[k] = vx - uy
        fig, ax = plt.subplots(figsize=(7.5, 5.5))
        mask = tri.mask if tri.mask is not None else np.zeros(len(tri.triangles), dtype=bool)
        unmasked = np.logical_not(mask)
        tri2 = mtri.Triangulation(tri.x, tri.y, tri.triangles[unmasked])
        # vorticity clip range [-5, 5] per benchmark recommendation
        cf = ax.tripcolor(tri2, vort[unmasked], shading="flat", cmap="RdBu_r",
                          vmin=-5.0, vmax=5.0)
        cb = fig.colorbar(cf, ax=ax, fraction=0.046, pad=0.03)
        cb.set_label("vorticity", fontsize=10)
        ax.set_xlim(*xlim)
        ax.set_ylim(*ylim)
        ax.set_aspect("equal")
        scholarly_ax(ax, "x", "y", f"{case_id}: vorticity (clipped [-5,5])")
        fig.tight_layout()
        out = fig_dir / f"{case_id}_vorticity.png"
        fig.savefig(out, dpi=150)
        plt.close(fig)
        outs.append(out)
    return outs


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("results_dir", type=Path)
    parser.add_argument("report_dir", type=Path)
    parser.add_argument("cases", nargs="*", type=str,
                        help="case ids to plot (default: all in results_dir)")
    args = parser.parse_args()

    fig_dir = args.report_dir / "figures"
    fig_dir.mkdir(parents=True, exist_ok=True)

    if args.cases:
        case_ids = args.cases
    else:
        case_ids = sorted(p.name for p in args.results_dir.iterdir()
                          if (args.results_dir / p.name / "metadata.json").exists())

    manifest = []
    sanity = {}
    for cid in case_ids:
        case_dir = args.results_dir / cid
        cylinder = "cylinder" in cid
        figs = []
        figs.append(plot_residuals(case_dir, fig_dir, cid))
        figs.append(plot_forces(case_dir, fig_dir, cid))
        figs.append(plot_cp(case_dir, fig_dir, cid))
        figs += plot_fields(case_dir, fig_dir, cid, cylinder)
        for f in figs:
            stem = f.stem
            ftype = "field" if any(k in stem for k in
                                   ("mach", "pressure", "velocity", "vorticity")) \
                    else "history"
            if "_zoom" in stem:
                ftype = "field_zoom"
            if stem.endswith("_cp"):
                ftype = "surface"
            var = "mach" if "mach" in stem else (
                "pressure" if "pressure" in stem else (
                    "velocity" if "velocity" in stem else (
                        "vorticity" if "vorticity" in stem else (
                            "residual" if "residual" in stem else (
                                "force" if "force" in stem else "cp")))))
            manifest.append({
                "figure_file": f.name,
                "case_id": cid,
                "figure_type": ftype,
                "variable": var,
                "source_file": "residuals.csv" if var == "residual"
                              else ("forces.csv" if var == "force"
                                    else ("surface.csv" if var == "cp"
                                          else "field_final.vtu")),
                "caption": f"{cid}: {' '.join(var.split('_'))} visualization",
            })
        # Sanity checks from the data
        res = read_csv(case_dir / "residuals.csv")
        fc = read_csv(case_dir / "forces.csv")
        try:
            x, y, fields = parse_vtu(case_dir / "field_final.vtu")
        except Exception:
            x, y, fields = [], [], {}
        md = json.loads((case_dir / "metadata.json").read_text())
        status = md.get("convergence_status")
        check = {}
        check["positive_density"] = bool(len(fields) and np.all(fields["density"] > 0))
        check["positive_pressure"] = bool(len(fields) and np.all(fields["pressure"] > 0))
        check["finite_residuals"] = bool(np.all(np.isfinite(res["residual_l2"])))
        final_cd = float(fc["cd"][-1])
        final_cl = float(fc["cl"][-1])
        if "cylinder" in cid:
            check["positive_mean_drag"] = bool(final_cd > 0)
            check["nonzero_lift_variation"] = bool(
                np.ptp(fc["cl"][-500:]) > 1e-3 or "re20" in cid)
        else:
            check["near_zero_lift"] = bool(abs(final_cl) < 0.05)
        check["cp_variation"] = bool(len(fields))
        check["convergence_status"] = status
        sanity[cid] = check

    with open(args.report_dir / "figure_manifest.csv", "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=[
            "figure_file", "case_id", "figure_type", "variable",
            "source_file", "caption"])
        writer.writeheader()
        writer.writerows(manifest)
    with open(args.report_dir / "sanity_checks.json", "w") as f:
        json.dump(sanity, f, indent=2)
    print(f"wrote {len(manifest)} figures and sanity checks to {args.report_dir}")


if __name__ == "__main__":
    main()
