#!/usr/bin/env python
"""Publication-style figure generation for one fv2d case result directory.

Produces:
  <case>_residual.png     semilogy residual history
  <case>_forces.png       cl / cd history (post-transient window marked)
  <case>_surface_cp.png   surface cp (vs x for airfoils, vs theta for cylinders)
  <case>_mach.png / <case>_mach_zoom.png
  <case>_pressure.png / <case>_pressure_zoom.png
  <case>_vorticity_wake.png   (cylinder cases; post-transient wake field)

Naming rule enforced here: *_mach.png always plots Mach, *_pressure.png
always plots pressure (the benchmark validator checks the manifest).

Also merges per-figure source metadata into <out-dir>/figure_sources.json
so make_figure_manifest.py can emit an accurate figure_manifest.csv.
"""
import argparse
import json
import os
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fv_plot
from fv_vtk import read_vtk

LW = 1.5
FIGSIZE = (7, 5)
DPI = 200


def setup_style():
    plt.rcParams.update({
        "font.size": 11,
        "axes.grid": True,
        "grid.alpha": 0.35,
        "lines.linewidth": LW,
        "figure.figsize": FIGSIZE,
        "savefig.dpi": DPI,
        "savefig.bbox": "tight",
        "legend.framealpha": 0.9,
    })


def _save(fig, out_dir, name, sources, entry):
    path = os.path.join(out_dir, name)
    fig.savefig(path)
    plt.close(fig)
    sources[name] = entry
    print("wrote", path)
    return name


def plot_residuals(info, case_dir, out_dir, sources):
    path = os.path.join(case_dir, fv_plot.CSV_RESIDUALS)
    if not os.path.isfile(path):
        print("skip residuals: no residuals.csv")
        return
    res = fv_plot.load_csv(path)
    if res["step"].size == 0:
        print("skip residuals: residuals.csv has no data rows")
        return
    cid = info["case_id"]
    transient = info["transient"]
    x = np.asarray(res["physical_time"] if transient else res["step"], float)
    xlabel = "physical time $t$" if transient else "iteration step"

    fig, ax = plt.subplots()
    for col in ("rho", "rhou", "rhov", "rhoE"):
        if col in res:
            ax.semilogy(x, np.abs(np.asarray(res[col], float)) + 1e-300,
                        lw=0.8, alpha=0.6, label=col)
    ax.semilogy(x, np.abs(np.asarray(res["residual_l2"], float)) + 1e-300,
                lw=LW, color="k", label="residual_l2")
    ax.set_xlabel(xlabel)
    ax.set_ylabel("residual (L2 norm)")
    ax.set_title(f"{cid}: residual history")
    ax.legend(loc="best", fontsize=9)
    return _save(fig, out_dir, f"{cid}_residual.png", sources, {
        "case_id": cid, "figure_type": "residual",
        "variable": "residual_l2", "source_file": fv_plot.CSV_RESIDUALS,
        "caption": f"Residual history (L2 and per-equation) vs {xlabel} for case {cid}.",
    })


def plot_forces(info, case_dir, out_dir, sources, transient_start):
    path = os.path.join(case_dir, fv_plot.CSV_FORCES)
    if not os.path.isfile(path):
        print("skip forces: no forces.csv")
        return
    f = fv_plot.load_csv(path)
    if f["step"].size == 0:
        print("skip forces: forces.csv has no data rows")
        return
    cid = info["case_id"]
    transient = info["transient"]
    x = np.asarray(f["physical_time"] if transient else f["step"], float)
    xlabel = "physical time $t$" if transient else "iteration step"
    cl = np.asarray(f["cl"], float)
    cd = np.asarray(f["cd"], float)

    fig, (ax1, ax2) = plt.subplots(2, 1, sharex=True, figsize=(7, 6))
    ax1.plot(x, cl, lw=LW, color="tab:blue")
    ax1.set_ylabel("$c_l$ (lift coefficient)")
    ax1.set_title(f"{cid}: force history")
    ax2.plot(x, cd, lw=LW, color="tab:red")
    ax2.set_ylabel("$c_d$ (drag coefficient)")
    ax2.set_xlabel(xlabel)
    if transient and transient_start < x.max():
        for ax in (ax1, ax2):
            ax.axvspan(transient_start, x.max(), color="orange", alpha=0.15,
                       label=f"post-transient ($t>{transient_start:g}$)")
            ax.legend(loc="best", fontsize=9)
    return _save(fig, out_dir, f"{cid}_forces.png", sources, {
        "case_id": cid, "figure_type": "forces",
        "variable": "cl,cd", "source_file": fv_plot.CSV_FORCES,
        "caption": f"Lift and drag coefficient history vs {xlabel} for case {cid}.",
    })


def plot_surface_cp(info, case_dir, out_dir, sources):
    path = os.path.join(case_dir, fv_plot.CSV_SURFACE)
    if not os.path.isfile(path):
        print("skip surface_cp: no surface.csv")
        return
    s = fv_plot.load_csv(path)
    if s["x"].size == 0:
        print("skip surface_cp: surface.csv has no data rows")
        return
    cid = info["case_id"]
    x = np.asarray(s["x"], float)
    y = np.asarray(s["y"], float)
    cp = np.asarray(s["cp"], float)

    fig, ax = plt.subplots()
    if info["kind"] == "cylinder":
        theta = np.degrees(np.arctan2(y, x))
        order = np.argsort(theta)
        cf = np.asarray(s["cf"], float)
        l1, = ax.plot(theta[order], cp[order], lw=LW, color="tab:blue",
                      label="$c_p$")
        ax.set_xlabel(r"angle $\theta=\mathrm{atan2}(y,x)$ [deg]")
        ax.set_ylabel("$c_p$ (pressure coefficient)", color="tab:blue")
        ax.tick_params(axis="y", labelcolor="tab:blue")
        ax2 = ax.twinx()
        l2, = ax2.plot(theta[order], cf[order], lw=LW, color="tab:red",
                       ls="--", label="$c_f$")
        ax2.set_ylabel("$c_f$ (skin friction)", color="tab:red")
        ax2.tick_params(axis="y", labelcolor="tab:red")
        ax2.grid(False)
        ax.legend(handles=[l1, l2], loc="best", fontsize=9)
        ax.set_title(f"{cid}: cylinder wall $c_p$ / $c_f$")
        variable = "cp,cf"
        caption = (f"Cylinder wall pressure coefficient and skin friction vs "
                   f"surface angle for case {cid}.")
    else:
        upper = y >= 0
        ax.plot(x[upper], cp[upper], "o", ms=2.5, color="tab:blue",
                label="upper side")
        ax.plot(x[~upper], cp[~upper], "s", ms=2.5, color="tab:orange",
                label="lower side")
        ax.invert_yaxis()  # aerodynamic convention
        ax.set_xlabel("$x/c$")
        ax.set_ylabel("$c_p$ (pressure coefficient)")
        ax.set_title(f"{cid}: surface pressure coefficient")
        ax.legend(loc="best", fontsize=9)
        variable = "cp"
        caption = f"Surface pressure coefficient vs chordwise position for case {cid}."
    return _save(fig, out_dir, f"{cid}_surface_cp.png", sources, {
        "case_id": cid, "figure_type": "surface_cp",
        "variable": variable, "source_file": fv_plot.CSV_SURFACE,
        "caption": caption,
    })


def _field_figure(info, triang, tri_cell, values, varname, cmap, out_dir,
                  sources, source_file, zoom, vmin=None, vmax=None,
                  clip_note="", vorticity=False):
    cid = info["case_id"]
    facecolors = np.asarray(values, float)[tri_cell]
    if vmin is None or vmax is None:
        vmin, vmax, clipped = fv_plot.clipped_range(facecolors)
        if clipped:
            clip_note = ("color range clipped to 1st-99th percentile"
                         + ("; " + clip_note if clip_note else ""))
    elif vorticity:
        clip_note = (f"vorticity clipped to [{vmin:g}, {vmax:g}]"
                     + ("; " + clip_note if clip_note else ""))

    fig, ax = plt.subplots()
    tpc = ax.tripcolor(triang, facecolors, cmap=cmap, vmin=vmin, vmax=vmax,
                       shading="flat")
    ax.set_aspect("equal")
    ax.set_xlabel("$x$")
    ax.set_ylabel("$y$")
    view = "full domain"
    if zoom is not None:
        ax.set_xlim(zoom[0], zoom[1])
        ax.set_ylim(zoom[2], zoom[3])
        view = "wake zoom" if info["kind"] == "cylinder" else "near-body zoom"
    title = f"{cid}: {varname} field ({view})"
    if clip_note:
        title += f"\n[{clip_note}]"
    ax.set_title(title, fontsize=10)
    cbar = fig.colorbar(tpc, ax=ax)
    cbar.set_label(varname)

    suffix = {"mach": "mach", "pressure": "pressure"}.get(varname, varname)
    if zoom is not None and not vorticity:
        suffix += "_zoom"
    if vorticity:
        suffix = "vorticity_wake"
    src = os.path.basename(source_file)
    if source_file and os.path.basename(os.path.dirname(source_file)) == "fields":
        src = "fields/" + src
    caption = (f"{varname} field ({view}) for case {cid} from {src}."
               + (f" {clip_note.capitalize()}." if clip_note else ""))
    return _save(fig, out_dir, f"{cid}_{suffix}.png", sources, {
        "case_id": cid, "figure_type": suffix,
        "variable": varname, "source_file": src,
        "caption": caption,
    })


def plot_fields(info, case_dir, out_dir, sources):
    path = os.path.join(case_dir, fv_plot.FIELD_FINAL)
    if not os.path.isfile(path):
        print("skip field plots: no field_final.vtk")
        return
    cid = info["case_id"]
    vtk = read_vtk(path)
    triang, tri_cell = fv_plot.build_triangulation(vtk)
    zoom = fv_plot.ZOOM_WINDOWS.get(info["kind"])

    for varname, cmap in (("mach", "viridis"), ("pressure", "viridis")):
        if varname not in vtk["cell_data"]:
            print(f"skip {varname}: variable missing from field_final.vtk")
            continue
        values = vtk["cell_data"][varname]
        vmin, vmax, clipped = fv_plot.clipped_range(values)
        note = "color range clipped to 1st-99th percentile" if clipped else ""
        _field_figure(info, triang, tri_cell, values, varname, cmap,
                      out_dir, sources, path, None, vmin, vmax, note)
        _field_figure(info, triang, tri_cell, values, varname, cmap,
                      out_dir, sources, path, zoom, vmin, vmax, note)


def plot_vorticity_wake(info, case_dir, out_dir, sources, vclip):
    if info["kind"] != "cylinder":
        return
    cid = info["case_id"]
    path, note = fv_plot.pick_wake_field(case_dir)
    if path is None:
        print("skip vorticity wake: no field file")
        return
    vtk = read_vtk(path)
    if "vorticity" not in vtk["cell_data"]:
        print("skip vorticity wake: no vorticity variable")
        return
    triang, tri_cell = fv_plot.build_triangulation(vtk)
    zoom = fv_plot.ZOOM_WINDOWS["cylinder"]
    _field_figure(info, triang, tri_cell, vtk["cell_data"]["vorticity"],
                  "vorticity", "seismic", out_dir, sources, path, zoom,
                  vmin=vclip[0], vmax=vclip[1], clip_note=note, vorticity=True)


def merge_sources(out_dir, sources):
    path = os.path.join(out_dir, "figure_sources.json")
    existing = {}
    if os.path.isfile(path):
        with open(path) as f:
            existing = json.load(f)
    existing.update(sources)
    with open(path, "w") as f:
        json.dump(existing, f, indent=2, sort_keys=True)
    print("updated", path)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--case-dir", required=True, help="case result directory")
    ap.add_argument("--out-dir", required=True, help="output figures directory")
    ap.add_argument("--transient-start", type=float, default=200.0,
                    help="start of post-transient window (default 200)")
    ap.add_argument("--vorticity-clip", type=float, nargs=2,
                    default=(-5.0, 5.0), metavar=("VMIN", "VMAX"),
                    help="vorticity clip range (default -5 5)")
    args = ap.parse_args()

    setup_style()
    os.makedirs(args.out_dir, exist_ok=True)
    info = fv_plot.classify_case(args.case_dir)
    print(f"case_id={info['case_id']} kind={info['kind']} "
          f"viscous={info['viscous']} transient={info['transient']}")

    sources = {}
    jobs = [
        ("residuals", lambda: plot_residuals(info, args.case_dir, args.out_dir, sources)),
        ("forces", lambda: plot_forces(info, args.case_dir, args.out_dir,
                                       sources, args.transient_start)),
        ("surface_cp", lambda: plot_surface_cp(info, args.case_dir, args.out_dir, sources)),
        ("fields", lambda: plot_fields(info, args.case_dir, args.out_dir, sources)),
        ("vorticity_wake", lambda: plot_vorticity_wake(
            info, args.case_dir, args.out_dir, sources, tuple(args.vorticity_clip))),
    ]
    for name, job in jobs:
        try:
            job()
        except Exception as exc:
            print(f"WARNING: {name} figure failed: {type(exc).__name__}: {exc}")
    merge_sources(args.out_dir, sources)


if __name__ == "__main__":
    main()
