#!/usr/bin/env python3
"""Generate report figures and sanity checks from solver output directories.

For each case output directory (containing residuals.csv, forces.csv,
surface.csv and field_final.vtu) this script writes:

  report/figures/<case>_residuals.png       residual history (semilogy)
  report/figures/<case>_forces.png          force history
  report/figures/<case>_cp.png              surface pressure coefficient
  report/figures/<case>_mach.png            Mach contours (full domain)
  report/figures/<case>_pressure.png        pressure contours (full domain)
  report/figures/<case>_mach_zoom.png       near-body Mach contours
  report/figures/<case>_pressure_zoom.png   near-body pressure contours
  report/figures/<case>_velocity.png        velocity magnitude (cylinder cases)
  report/figures/<case>_vorticity.png       vorticity (cylinder Re200)

and a figure manifest (figure_manifest.csv) plus the machine-readable
sanity_checks.json.

Usage:
  python3 plot_results.py results_dir report_dir [case_dir ...]
"""

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


NS = {"vtk": "http://www.w3.org/1999/xlink"}


def parse_vtu(path: Path):
    """Return (x, y, fields) from the ASCII single-piece VTU written by the solver."""
    tree = ET.parse(path)
    root = tree.getroot()
    piece = root.find(".//UnstructuredGrid/Piece")
    npts = int(piece.get("NumberOfPoints"))
    arrays = {}
    for da in piece.findall(".//DataArray"):
        name = da.get("Name")
        ncomp = int(da.get("NumberOfComponents", "1"))
        vals = np.array([float(v) for v in da.text.split()])
        if ncomp > 1:
            vals = vals.reshape(-1, ncomp)
        if name:
            arrays[name] = vals
    # The point coordinate block is <Points><DataArray .../></Points>; the
    # array itself carries no Name attribute.
    pts_da = root.find(".//UnstructuredGrid/Piece/Points/DataArray")
    ncomp = int(pts_da.get("NumberOfComponents", "3"))
    pts = np.array([float(v) for v in pts_da.text.split()]).reshape(-1, ncomp)
    x = pts[:, 0]
    y = pts[:, 1]
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
            # keep string columns (e.g. surface tag) out of numeric arrays
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
    """Upper-surface NACA0012 thickness (chord-normalized, x in [0,1])."""
    x = np.clip(x, 0.0, 1.0)
    sx = np.sqrt(x)
    return 0.6 * (0.2969 * sx - 0.1260 * x - 0.3516 * x * x +
                  0.2843 * x * x * x - 0.1015 * x * x * x * x)


def body_mask(tri, x, y, cylinder):
    """Mask Delaunay triangles whose centroid falls inside the solid body.

    The solver writes cell-centered data, so the Delaunay triangulation of
    the cell centroids bridges across the airfoil/cylinder interior.  Those
    triangles must not be contoured.
    """
    txc, tyc = tri.x[tri.triangles].mean(axis=1), tri.y[tri.triangles].mean(axis=1)
    if cylinder:
        inside = np.hypot(txc, tyc) < 0.5
    else:
        # NACA 4-digit camber line is zero for a symmetric airfoil
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
    # Fixed physical Mach range per freestream Mach (clipped with colorbar
    # extend arrows so outliers do not collapse the scale).
    mach_inf = 0.15
    for tok, val in (("m015", 0.15), ("m080", 0.8), ("m200", 2.0)):
        if tok in case_id:
            mach_inf = val
    mach_vmax = max(0.2, 1.35 * mach_inf)
    mach_vmin = 0.0
    # full-domain views
    outs.append(plot_field_contours(x, y, mach, fig_dir, case_id, "mach",
                                    "Mach number", levels=41,
                                    title=f"{case_id}: Mach contours",
                                    cylinder=cylinder, vmin=mach_vmin,
                                    vmax=mach_vmax))
    outs.append(plot_field_contours(x, y, pres, fig_dir, case_id, "pressure",
                                    "pressure", levels=41,
                                    title=f"{case_id}: pressure contours",
                                    cylinder=cylinder))
    # near-body zooms
    if cylinder:
        xlim = (-2.5, 6.0)
        ylim = (-3.0, 3.0)
    else:
        xlim = (-0.6, 1.6)
        ylim = (-0.8, 0.8)
    outs.append(plot_field_contours(x, y, mach, fig_dir, case_id, "mach_zoom",
                                    "Mach number", xlim=xlim, ylim=ylim, levels=41,
                                    title=f"{case_id}: near-body Mach contours",
                                    cylinder=cylinder, vmin=mach_vmin,
                                    vmax=mach_vmax))
    outs.append(plot_field_contours(x, y, pres, fig_dir, case_id, "pressure_zoom",
                                    "pressure", xlim=xlim, ylim=ylim, levels=41,
                                    title=f"{case_id}: near-body pressure contours",
                                    cylinder=cylinder))
    # velocity magnitude (cylinder cases)
    if cylinder:
        vel = np.hypot(fields["velocity_x"], fields["velocity_y"])
        outs.append(plot_field_contours(x, y, vel, fig_dir, case_id, "velocity",
                                        "velocity magnitude", xlim=xlim, ylim=ylim,
                                        levels=41, cmap="viridis",
                                        title=f"{case_id}: velocity magnitude",
                                        cylinder=True))
        # vorticity from the piecewise-linear Delaunay interpolant
        tri = mtri.Triangulation(x, y)
        tri = body_mask(tri, x, y, True)
        u = fields["velocity_x"]
        v = fields["velocity_y"]
        vort = np.zeros(len(tri.triangles))
        for k, (i, j, m) in enumerate(tri.triangles):
            d = np.array([[x[j] - x[i], y[j] - y[i]],
                          [x[m] - x[i], y[m] - y[i]]])
            try:
                inv = np.linalg.inv(d)
            except np.linalg.LinAlgError:
                continue
            du = inv @ np.array([u[j] - u[i], u[m] - u[i]])
            dv = inv @ np.array([v[j] - v[i], v[m] - v[i]])
            vort[k] = dv[0] - du[1]
        fig, ax = plt.subplots(figsize=(7.5, 5.5))
        tc = ax.tripcolor(tri, vort, shading="flat", cmap="RdBu_r",
                          vmin=-5.0, vmax=5.0)
        cb = fig.colorbar(tc, ax=ax, fraction=0.046, pad=0.03)
        cb.set_label(r"vorticity $\omega_z$", fontsize=10)
        ax.set_xlim(xlim)
        ax.set_ylim(ylim)
        ax.set_aspect("equal")
        scholarly_ax(ax, "x", "y", f"{case_id}: wake vorticity (clipped [-5, 5])")
        fig.tight_layout()
        out = fig_dir / f"{case_id}_vorticity.png"
        fig.savefig(out, dpi=150)
        plt.close(fig)
        outs.append(out)
    return outs


def run_sanity_checks(case_dirs, report_dir, figure_map):
    checks = {}
    ok = True
    for case_dir in case_dirs:
        case_id = case_dir.name
        entry = {}
        entry["output_dir"] = str(case_dir)
        try:
            md = json.loads((case_dir / "metadata.json").read_text())
            status = md.get("convergence_status", "missing")
            entry["convergence_status"] = status
        except Exception as e:
            entry["convergence_status"] = f"error: {e}"
            status = "error"
        # field positivity
        vtu = case_dir / "field_final.vtu"
        if vtu.exists():
            _, _, fields = parse_vtu(vtu)
            rho = fields["density"]
            p = fields["pressure"]
            entry["min_density"] = float(np.min(rho))
            entry["min_pressure"] = float(np.min(p))
            entry["all_positive_density_pressure"] = bool(
                np.all(rho > 0) and np.all(p > 0))
            entry["field_finite"] = bool(np.all(np.isfinite(rho)) and
                                         np.all(np.isfinite(p)) and
                                         np.all(np.isfinite(fields["mach"])))
        else:
            entry["all_positive_density_pressure"] = False
            entry["field_finite"] = False
        # forces
        try:
            fc = read_csv(case_dir / "forces.csv")
            entry["final_cl"] = float(fc["cl"][-1])
            entry["final_cd"] = float(fc["cd"][-1])
            entry["final_viscous_drag"] = float(fc["viscous_drag"][-1])
            entry["final_viscous_lift"] = float(fc["viscous_lift"][-1])
            entry["mean_cd_last100"] = float(np.mean(fc["cd"][-100:]))
            entry["std_cl_last100"] = float(np.std(fc["cl"][-100:]))
            entry["forces_finite"] = bool(np.all(np.isfinite(fc["cd"])) and
                                          np.all(np.isfinite(fc["cl"])))
        except Exception as e:
            entry["forces_finite"] = False
            entry["forces_error"] = str(e)
        # surface
        try:
            surf = read_csv(case_dir / "surface.csv")
            entry["cp_variation"] = float(np.ptp(surf["cp"]))
            entry["cp_varies"] = bool(np.ptp(surf["cp"]) > 1e-3)
            entry["surface_finite"] = bool(np.all(np.isfinite(surf["pressure"])))
            wall_speed = np.hypot(surf["u"], surf["v"])
            entry["max_wall_speed"] = float(np.max(wall_speed))
            entry["max_skin_friction"] = float(np.max(surf["cf"])) if len(surf["cf"]) else 0.0
        except Exception as e:
            entry["surface_finite"] = False
            entry["surface_error"] = str(e)
        checks[case_id] = entry
        # aggregate checks
        ok = ok and entry.get("field_finite", False)
    return checks, ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("results_dir", type=Path)
    ap.add_argument("report_dir", type=Path)
    ap.add_argument("case_dirs", nargs="*", type=Path)
    args = ap.parse_args()

    results = args.results_dir
    if args.case_dirs:
        case_dirs = args.case_dirs
    else:
        case_dirs = [d for d in sorted(results.iterdir()) if d.is_dir()]

    fig_dir = args.report_dir / "figures"
    fig_dir.mkdir(parents=True, exist_ok=True)

    manifest = []
    fig_map = {}
    for case_dir in case_dirs:
        case_id = case_dir.name
        if not (case_dir / "residuals.csv").exists():
            continue
        is_cyl = "cylinder" in case_id
        for out, ftype, var, src in [
            (plot_residuals(case_dir, fig_dir, case_id), "residual_history",
             "residual_l2", "residuals.csv"),
            (plot_forces(case_dir, fig_dir, case_id), "force_history",
             "cd,cl,cmz", "forces.csv"),
            (plot_cp(case_dir, fig_dir, case_id), "surface_cp",
             "cp", "surface.csv"),
        ]:
            manifest.append([out.name, case_id, ftype, var, src,
                             f"{case_id} {ftype.replace('_', ' ')}"])
            fig_map[out.name] = (case_id, ftype, var, src)
        for out in plot_fields(case_dir, fig_dir, case_id, is_cyl):
            var = out.stem.split("_")[-1]
            if var == "zoom":
                var = out.stem.split("_")[-2]
            manifest.append([out.name, case_id, "field", var, "field_final.vtu",
                             f"{case_id} {var} field contours"])
            fig_map[out.name] = (case_id, "field", var, "field_final.vtu")

    with open(args.report_dir / "figure_manifest.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["figure_file", "case_id", "figure_type", "variable",
                    "source_file", "caption"])
        w.writerows(manifest)

    sanity, _ = run_sanity_checks(case_dirs, args.report_dir, fig_map)
    (args.report_dir / "sanity_checks.json").write_text(
        json.dumps(sanity, indent=2) + "\n")

    print(f"wrote {len(manifest)} figures to {fig_dir}")
    print(f"wrote figure_manifest.csv and sanity_checks.json")


if __name__ == "__main__":
    main()
