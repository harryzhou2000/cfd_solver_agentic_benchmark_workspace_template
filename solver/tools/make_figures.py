#!/usr/bin/env python3
"""Generate every report figure from the submitted solver output.

Each figure is written to <out>/ and registered in a CSV manifest that maps it
to the case, the plotted variable and the source data file, so that every
figure in the report is traceable to a file produced by an actual solver run.

Usage:
    .venv/bin/python tools/make_figures.py --results results --out report/figures \
        --manifest report/figure_manifest.csv
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import matplotlib.pyplot as plt  # noqa: E402
from plot_style import (add_colorbar, apply_style, draw_body, field_contour, SERIES)  # noqa: E402
from vtu_reader import read_vtu  # noqa: E402

# View windows: (xmin, xmax, ymin, ymax)
NACA_NEAR = (-0.6, 1.8, -0.9, 0.9)
NACA_WAKE = (-0.4, 2.8, -0.7, 0.7)
CYL_NEAR = (-2.0, 6.0, -2.5, 2.5)
CYL_WAKE = (-2.0, 14.0, -3.5, 3.5)


class Manifest:
    def __init__(self):
        self.rows = []

    def add(self, figure_file, case_id, figure_type, variable, source_file, caption):
        self.rows.append(dict(figure_file=figure_file, case_id=case_id, figure_type=figure_type,
                              variable=variable, source_file=source_file, caption=caption))

    def write(self, path, merge=False):
        # When only a subset of cases was regenerated, keep the rows of the
        # cases that were not touched: overwriting the manifest with just this
        # invocation's rows would silently truncate it.
        if merge and os.path.exists(path):
            mine = {r["figure_file"] for r in self.rows}
            with open(path, newline="") as f:
                keep = [r for r in csv.DictReader(f) if r["figure_file"] not in mine]
            self.rows = sorted(self.rows + keep, key=lambda r: r["figure_file"])
        os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
        with open(path, "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=["figure_file", "case_id", "figure_type", "variable",
                                              "source_file", "caption"])
            w.writeheader()
            w.writerows(self.rows)
        with open(os.path.splitext(path)[0] + ".json", "w") as f:
            json.dump(self.rows, f, indent=2)


def load_csv(path):
    with open(path, newline="") as f:
        rows = list(csv.DictReader(f))
    if not rows:
        return {}
    out = {}
    for k in rows[0]:
        try:
            out[k] = np.array([float(r[k]) for r in rows])
        except ValueError:
            out[k] = np.array([r[k] for r in rows])
    return out


# --------------------------------------------------------------------- plots
def plot_residuals(case_dir, cid, meta, out, man):
    res = load_csv(os.path.join(case_dir, "residuals.csv"))
    transient = float(meta.get("physical_time_step", 0.0)) > 0.0
    x = res["physical_time"] if transient else res["step"]
    xlabel = r"physical time $t\,U_\infty/L_{\mathrm{ref}}$" if transient else "pseudo-time step"
    fig, axes = plt.subplots(1, 2 if transient else 1, figsize=(11 if transient else 6.4, 4.0))
    axes = np.atleast_1d(axes)
    ax = axes[0]
    names = [("rho", r"$\rho$"), ("rhou", r"$\rho u$"), ("rhov", r"$\rho v$"),
             ("rhoE", r"$\rho E$")]
    for i, (k, lbl) in enumerate(names):
        ax.semilogy(x, np.maximum(res[k], 1e-300), color=SERIES[i], lw=1.3, label=lbl)
    ax.semilogy(x, np.maximum(res["residual_l2"], 1e-300), color="k", lw=1.8,
                label=r"total $L_2$")
    # Mark the pseudo-time step at which the limiter values were frozen; on the
    # shock cases the residual drops sharply there and the reader should be able
    # to see that it is the freeze and not a coincidence.
    fs = int(meta.get("limiter_freeze_step", 0) or 0)
    if not transient and fs > 0 and fs < float(np.max(x)):
        ax.axvline(fs, color="0.35", ls=":", lw=1.4,
                   label=f"limiter frozen (step {fs})")
    ax.set_xlabel(xlabel)
    ax.set_ylabel(r"scaled residual $\|R/(V s_k)\|$")
    ax.set_title(f"{cid}: residual history")
    ax.legend(ncol=2, loc="best")
    if transient:
        ax2 = axes[1]
        ax2.plot(res["physical_time"], res["inner_iter"], color=SERIES[0], lw=1.2,
                 label="inner iterations")
        ax2.axhline(float(np.mean(res["inner_iter"][1:])), color="k", ls="--", lw=0.9,
                    label=fr"mean {np.mean(res['inner_iter'][1:]):.1f}")
        ax2.legend()
        ax2.set_xlabel(xlabel)
        ax2.set_ylabel("inner iterations per physical step")
        ax2.set_title(f"{cid}: dual-time inner iterations")
    fig.tight_layout()
    f = f"{cid}_residuals.png"
    fig.savefig(os.path.join(out, f))
    plt.close(fig)
    man.add(f, cid, "line", "residual_l2", f"results/{cid}/residuals.csv",
            f"Residual history for {cid}: per-equation and total scaled $L_2$ residual"
            + (" and inner-iteration count per physical step." if transient else "."))


def plot_forces(case_dir, cid, meta, out, man):
    fc = load_csv(os.path.join(case_dir, "forces.csv"))
    transient = float(meta.get("physical_time_step", 0.0)) > 0.0
    x = fc["physical_time"] if transient else fc["step"]
    xlabel = r"physical time $t\,U_\infty/L_{\mathrm{ref}}$" if transient else "pseudo-time step"
    fig, axes = plt.subplots(1, 2, figsize=(11, 4.0))
    axes[0].plot(x, fc["cd"], color=SERIES[1], label=r"$C_D$ (total)")
    axes[0].plot(x, fc["pressure_drag"], color=SERIES[0], ls="--", lw=1.3,
                 label=r"$C_{D,p}$ (pressure)")
    if np.max(np.abs(fc["viscous_drag"])) > 1e-12:
        axes[0].plot(x, fc["viscous_drag"], color=SERIES[2], ls=":", lw=1.3,
                     label=r"$C_{D,f}$ (skin friction)")
    axes[0].set_xlabel(xlabel)
    axes[0].set_ylabel(r"drag coefficient $C_D$")
    axes[0].set_title(f"{cid}: drag history")
    axes[0].legend()
    axes[1].plot(x, fc["cl"], color=SERIES[0], label=r"$C_L$")
    axes[1].plot(x, fc["cmz"], color=SERIES[3], lw=1.3, label=r"$C_{m,z}$")
    axes[1].set_xlabel(xlabel)
    axes[1].set_ylabel(r"$C_L$, $C_{m,z}$")
    axes[1].set_title(f"{cid}: lift and moment history")
    axes[1].legend()
    if not transient:
        for a in axes:
            a.set_xscale("symlog", linthresh=10)
    fig.tight_layout()
    f = f"{cid}_forces.png"
    fig.savefig(os.path.join(out, f))
    plt.close(fig)
    man.add(f, cid, "line", "force_coefficients", f"results/{cid}/forces.csv",
            f"Force-coefficient history for {cid}: total drag with its pressure/skin-friction "
            f"split, lift and pitching moment.")


def read_surface(case_dir):
    rows = list(csv.DictReader(open(os.path.join(case_dir, "surface.csv"), newline="")))
    return rows


def plot_surface_naca(case_dir, cid, meta, out, man, viscous):
    rows = [r for r in read_surface(case_dir)]
    x = np.array([float(r["x"]) for r in rows])
    y = np.array([float(r["y"]) for r in rows])
    cp = np.array([float(r["cp"]) for r in rows])
    cf = np.array([float(r["cf"]) for r in rows])
    ny = np.array([float(r["ny"]) for r in rows])
    chord = x.max() - x.min()
    xc = (x - x.min()) / chord
    upper = ny < 0.0     # fluid-outward normal points down on the upper surface
    lower = ~upper
    fig, axes = plt.subplots(1, 2 if viscous else 1, figsize=(11 if viscous else 6.0, 4.2))
    axes = np.atleast_1d(axes)
    ax = axes[0]
    # At zero incidence the two surfaces coincide, so the lower surface is drawn
    # dashed with open markers; a solid line hidden under it would look like a
    # single curve and hide any upper/lower asymmetry.
    STYLE = ((upper, "upper surface", SERIES[0], "o", "-", None),
             (lower, "lower surface", SERIES[1], "s", "--", "none"))
    for sel, lbl, c, m, ls, mfc in STYLE:
        o = np.argsort(xc[sel])
        ax.plot(xc[sel][o], cp[sel][o], color=c, marker=m, ms=2.8, lw=1.2, ls=ls,
                markerfacecolor=mfc, markevery=3, label=lbl)
    ax.invert_yaxis()
    ax.set_xlabel(r"$x/c$")
    ax.set_ylabel(r"pressure coefficient $C_p$")
    ax.set_title(f"{cid}: surface pressure coefficient")
    ax.legend()
    if viscous:
        ax = axes[1]
        for sel, lbl, c, m, ls, mfc in STYLE:
            o = np.argsort(xc[sel])
            ax.plot(xc[sel][o], cf[sel][o], color=c, marker=m, ms=2.8, lw=1.2, ls=ls,
                    markerfacecolor=mfc, markevery=3, label=lbl)
        ax.axhline(0.0, color="k", lw=1.1, ls="--", label="separation ($C_f=0$)")
        ax.set_xlabel(r"$x/c$")
        ax.set_ylabel(r"skin-friction coefficient $C_f$")
        ax.set_title(f"{cid}: skin-friction distribution")
        ax.legend()
    fig.tight_layout()
    f = f"{cid}_surface_cp.png"
    fig.savefig(os.path.join(out, f))
    plt.close(fig)
    man.add(f, cid, "line", "surface_cp" + ("_and_cf" if viscous else ""),
            f"results/{cid}/surface.csv",
            f"Wall distributions for {cid}: pressure coefficient"
            + (" and skin-friction coefficient" if viscous else "")
            + " on the upper and lower airfoil surfaces.")
    return np.column_stack([x, y])


def plot_surface_cylinder(case_dir, cid, meta, out, man, viscous):
    rows = read_surface(case_dir)
    x = np.array([float(r["x"]) for r in rows])
    y = np.array([float(r["y"]) for r in rows])
    cp = np.array([float(r["cp"]) for r in rows])
    cf = np.array([float(r["cf"]) for r in rows])
    theta = np.degrees(np.arctan2(y, x)) % 360.0
    o = np.argsort(theta)
    fig, axes = plt.subplots(1, 2 if viscous else 1, figsize=(11 if viscous else 6.0, 4.2))
    axes = np.atleast_1d(axes)
    axes[0].plot(theta[o], cp[o], color=SERIES[0], marker="o", ms=2.6, lw=1.3,
                 label=r"$C_p$ (wall value)")
    axes[0].axhline(0.0, color="k", lw=0.8, ls=":")
    axes[0].legend()
    axes[0].set_xlabel(r"azimuth $\theta$ [deg] (0$^\circ$ = rear stagnation line, downstream)")
    axes[0].set_ylabel(r"pressure coefficient $C_p$")
    axes[0].set_title(f"{cid}: wall pressure coefficient")
    axes[0].set_xlim(0, 360)
    axes[0].set_xticks(np.arange(0, 361, 45))
    if viscous:
        axes[1].plot(theta[o], cf[o], color=SERIES[1], marker="s", ms=2.6, lw=1.3,
                     label=r"$C_f$ (tangential wall traction)")
        axes[1].axhline(0.0, color="k", lw=0.9, ls="--", label="separation ($C_f=0$)")
        axes[1].legend()
        axes[1].set_xlabel(r"azimuth $\theta$ [deg]")
        axes[1].set_ylabel(r"skin-friction coefficient $C_f$")
        axes[1].set_title(f"{cid}: wall skin friction")
        axes[1].set_xlim(0, 360)
        axes[1].set_xticks(np.arange(0, 361, 45))
    fig.tight_layout()
    f = f"{cid}_surface_cp.png"
    fig.savefig(os.path.join(out, f))
    plt.close(fig)
    man.add(f, cid, "line", "surface_cp" + ("_and_cf" if viscous else ""),
            f"results/{cid}/surface.csv",
            f"Cylinder wall distributions for {cid}: pressure coefficient"
            + (" and skin-friction coefficient" if viscous else "") + " versus azimuth.")
    return np.column_stack([x, y])


def plot_field(mesh, values, cid, out, man, name, variable, cbar_label, caption, window,
               body=None, cmap="viridis", clip=None, vmin=None, vmax=None, lines=0,
               source=None, wide=None, wide_caption=None):
    fig, ax = plt.subplots(figsize=(7.2, 5.0))
    cf = field_contour(ax, mesh, values, window=window, cmap=cmap, clip_percentile=clip,
                       vmin=vmin, vmax=vmax, lines=lines)
    draw_body(ax, body)
    add_colorbar(fig, cf, ax, cbar_label)
    ax.set_xlabel(r"$x/L_{\mathrm{ref}}$")
    ax.set_ylabel(r"$y/L_{\mathrm{ref}}$")
    ax.set_title(f"{cid}: {cbar_label}")
    ax.grid(False)
    fig.tight_layout()
    f = f"{cid}_{name}.png"
    fig.savefig(os.path.join(out, f))
    plt.close(fig)
    man.add(f, cid, "contour", variable, source or f"results/{cid}/field_final.vtu", caption)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--results", default="results")
    ap.add_argument("--out", default="report/figures")
    ap.add_argument("--manifest", default="report/figure_manifest.csv")
    ap.add_argument("--cases", nargs="*", default=None)
    args = ap.parse_args()

    apply_style()
    os.makedirs(args.out, exist_ok=True)
    man = Manifest()

    case_dirs = sorted(d for d in os.listdir(args.results)
                       if os.path.isdir(os.path.join(args.results, d)) and not d.startswith("_"))
    if args.cases:
        case_dirs = [d for d in case_dirs if d in args.cases]

    for cid in case_dirs:
        cdir = os.path.join(args.results, cid)
        mpath = os.path.join(cdir, "metadata.json")
        if not os.path.exists(mpath):
            continue
        meta = json.load(open(mpath))
        viscous = "disabled" not in str(meta.get("viscous_flux", ""))
        is_cyl = "cylinder" in cid
        print(f"[figures] {cid}")

        plot_residuals(cdir, cid, meta, args.out, man)
        plot_forces(cdir, cid, meta, args.out, man)
        body = (plot_surface_cylinder if is_cyl else plot_surface_naca)(
            cdir, cid, meta, args.out, man, viscous)

        mesh = read_vtu(os.path.join(cdir, "field_final.vtu"))
        near = CYL_NEAR if is_cyl else NACA_NEAR
        wake = CYL_WAKE if is_cyl else NACA_WAKE

        # Clip the colour range to the 0.2-99.8 percentile of the plotted window
        # so that a couple of cells inside a shock or at the stagnation point
        # cannot collapse the range for the rest of the field.  Stated in the
        # caption, as required by the visualisation guidelines.
        clip = (1.0, 99.0)
        clip_note = (" Colour range clipped to the 1--99 percentile of the plotted window.")
        plot_field(mesh, mesh.cell_data["Mach"], cid, args.out, man, "mach", "mach",
                   r"Mach number $M$",
                   f"Mach-number contours near the body for {cid}, from the final field file."
                   + clip_note,
                   near, body=body, cmap="turbo", lines=12, clip=clip)
        plot_field(mesh, mesh.cell_data["Pressure"], cid, args.out, man, "pressure", "pressure",
                   r"static pressure $p$",
                   f"Static-pressure contours near the body for {cid}, from the final field file."
                   + clip_note,
                   near, body=body, cmap="plasma", lines=12, clip=clip)
        plot_field(mesh, mesh.cell_data["VelocityMagnitude"], cid, args.out, man,
                   "velocity_magnitude", "velocity_magnitude",
                   r"velocity magnitude $|\mathbf{u}|/U_\infty$",
                   f"Velocity-magnitude contours for {cid} over the wake window." + clip_note,
                   wake, body=body, cmap="viridis", lines=0, clip=clip)
        vort = mesh.cell_data["Vorticity"]
        vort_clip = 5.0 if is_cyl else 20.0
        plot_field(mesh, vort, cid, args.out, man, "vorticity", "vorticity",
                   r"vorticity $\omega_z L_{\mathrm{ref}}/U_\infty$",
                   f"Vorticity contours for {cid}, clipped to the symmetric range "
                   f"$[{-vort_clip:g},{vort_clip:g}]$ so that the wake structure is visible.",
                   wake, body=body, cmap="RdBu_r", vmin=-vort_clip, vmax=vort_clip)

        # Whole-domain overview so the farfield treatment can be inspected.
        wide_win = (-25, 45, -25, 25) if is_cyl else (-12, 20, -12, 12)
        plot_field(mesh, mesh.cell_data["Mach"], cid, args.out, man, "mach_farfield", "mach",
                   r"Mach number $M$",
                   f"Wide view of the Mach-number field for {cid}, showing recovery towards the "
                   f"freestream away from the body." + clip_note,
                   wide_win, body=body, cmap="turbo", clip=clip)

        # Partition map from the same field file.
        if float(meta.get("mpi_ranks", 1)) > 1:
            plot_field(mesh, mesh.cell_data["RankId"], cid, args.out, man, "partition",
                       "mpi_rank_id", "MPI rank id",
                       f"METIS k-way partition actually used by the {int(meta['mpi_ranks'])}-rank "
                       f"run of {cid}; colour is the owning rank of each cell.",
                       near, body=body, cmap="tab20", lines=0)

    # Register the cross-case figures produced by the other tools, so that every
    # figure referenced by the report is traceable through the manifest.
    fm = os.path.join(args.results, "naca0012_m080_inviscid", "metadata.json")
    freeze_m080 = (int(json.load(open(fm)).get("limiter_freeze_step", 0) or 0)
                   if os.path.exists(fm) else 0)
    extra = [
        ("mpi_scaling.png", "mpi_rank_study", "line", "parallel_speedup_and_force_consistency",
         "report/mpi_study.csv",
         "MPI rank-count study: parallel speed-up, absolute drag difference against the "
         "single-rank run compared with the force-stationarity tolerance, halo size and "
         "METIS edge cut."),
        ("cylinder_re200_shedding.png", "cylinder_m010_laminar_re200", "line",
         "lift_drag_and_spectrum",
         "results/cylinder_m010_laminar_re200/forces.csv",
         "Post-transient lift and drag oscillations and the Hann-windowed lift spectrum used "
         "to extract the shedding frequency and Strouhal number."),
        ("limiter_study.png", "naca0012_m080_inviscid", "line",
         "residual_and_drag_limiter_study",
         "results/naca0012_m080_inviscid/residuals.csv",
         "Mach 0.8 aerofoil: residual and drag history with the limiter recomputed at every "
         f"step (bounded limit cycle) and with the limiter frozen from step {freeze_m080}, "
         "after which the residual reaches the requested four-order reduction."),
        ("mms_order.png", "verification", "line", "manufactured_solution_error",
         "report/verification.json",
         "Manufactured-solution discretisation error against mean cell size for the "
         "first-order, limited second-order and unlimited second-order schemes."),
    ]
    for fn, cid, ftype, var, src, cap in extra:
        if os.path.exists(os.path.join(args.out, fn)):
            man.add(fn, cid, ftype, var, src, cap)

    man.write(args.manifest, merge=bool(args.cases))
    print(f"[figures] wrote {len(man.rows)} figures and {args.manifest}")


if __name__ == "__main__":
    main()
