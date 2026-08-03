#!/usr/bin/env python3
"""Plot final flow-field contours for one solver case.

Usage:
    python plot_field.py <case_dir> [case_id] [figures_dir]

Outputs (in <figures_dir>):
    <case_id>_mach.png      Mach number contours
    <case_id>_pressure.png  static pressure contours
    <case_id>_vorticity.png vorticity contours (viscous cases only)

Cell-centered data is rendered with ``tricontourf`` over the (triangulated)
unstructured mesh.  Vorticity is computed as dv/dx - du/dy from
adjacent-cell gradients; for the Re200 cylinder the recommended clip range
[-5, 5] from the case configuration is used.
"""

import argparse
import os
import sys

import matplotlib.pyplot as plt
import numpy as np
from matplotlib.tri import Triangulation

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from plot_utils import (build_triangles, cell_centroids, cell_to_vertex,
                        compute_vorticity, ensure_figures_dir, figure_path,
                        is_transient, is_viscous, read_vtu,
                        setup_figure)  # noqa: E402

RE200_VORTICITY_CLIP = (-5.0, 5.0)  # from cylinder_m010_laminar_re200.json
MAX_CELLS = 60000                   # down-sample huge meshes for rendering
N_LEVELS = 41


def _field_plot(vtu, values, cmap, title, cb_label, clip=None,
                vmin=None, vmax=None, figsize=(6.5, 4.6)):
    # Down-sample very large meshes (keeps cells, drops points they owned).
    cells = vtu["cells"]
    n = len(cells)
    if n > MAX_CELLS:
        stride = int(np.ceil(n / MAX_CELLS))
        keep = np.arange(0, n, stride)
        cells = [cells[k] for k in keep]
    pts = vtu["points"]
    tris = build_triangles({**vtu, "cells": cells})[0]
    z = cell_to_vertex(values, cells, pts)

    fig, ax = plt.subplots(figsize=figsize)
    if clip is not None:
        vmin, vmax = clip
    tri = Triangulation(pts[:, 0], pts[:, 1], tris)
    cont = ax.tricontourf(tri, z,
                          levels=N_LEVELS, cmap=cmap,
                          vmin=vmin, vmax=vmax)
    ax.tricontour(tri, z, levels=9,
                  colors="0.25", linewidths=0.25, alpha=0.5)
    ax.set_aspect("equal", adjustable="datalim")
    ax.set_xlabel(r"$x$")
    ax.set_ylabel(r"$y$")
    ax.set_title(title, pad=8)
    cb = fig.colorbar(cont, ax=ax, pad=0.02, aspect=30)
    cb.set_label(cb_label)
    cb.ax.tick_params(labelsize=8)
    return fig


def _symmetric_limits(values, quantile=0.99):
    """Symmetric color limits from the 99th percentile (robust to spikes)."""
    finite = values[np.isfinite(values)]
    if finite.size == 0:
        return -1.0, 1.0
    limit = float(np.quantile(np.abs(finite), quantile))
    if not np.isfinite(limit) or limit <= 0.0:
        limit = 1.0
    return -limit, limit


def plot_field(case_dir, case_id, figures_dir):
    vtu_path = os.path.join(case_dir, "field_final.vtu")
    vtu = read_vtu(vtu_path)
    if vtu is None:
        print(f"  SKIP {case_id}: no field_final.vtu")
        return False

    cd = vtu["cell_data"]
    n = len(vtu["cells"])
    if n == 0 or "mach" not in cd or "pressure" not in cd:
        print(f"  SKIP {case_id}: VTU has no usable cell data")
        return False

    mach = cd["mach"][:, 0] if cd["mach"].ndim > 1 else cd["mach"]
    pressure = cd["pressure"][:, 0] if cd["pressure"].ndim > 1 else cd["pressure"]

    n_figs = 0

    # --- Mach number -------------------------------------------------------
    fig = _field_plot(vtu, mach, "viridis",
                      f"{case_id} — Mach number",
                      r"Mach number")
    out = figure_path(figures_dir, case_id, "mach")
    fig.savefig(out)
    plt.close(fig)
    print(f"  wrote {os.path.relpath(out)}")
    n_figs += 1

    # --- Pressure ----------------------------------------------------------
    pmin = float(np.min(pressure))
    pmax = float(np.max(pressure))
    fig = _field_plot(vtu, pressure, "plasma",
                      f"{case_id} — static pressure",
                      r"$p$", vmin=pmin, vmax=pmax)
    out = figure_path(figures_dir, case_id, "pressure")
    fig.savefig(out)
    plt.close(fig)
    print(f"  wrote {os.path.relpath(out)}")
    n_figs += 1

    # --- Vorticity (viscous cases only) ------------------------------------
    if is_viscous(case_id):
        velocity = cd["velocity"]
        if velocity.ndim > 1 and velocity.shape[1] >= 2:
            centroids = cell_centroids(vtu)
            vort = compute_vorticity(
                velocity[:, :2], vtu["cells"], centroids, vtu["points"])
            clip = RE200_VORTICITY_CLIP if is_transient(case_id) else None
            vmin, vmax = (clip if clip is not None
                          else _symmetric_limits(vort))
            fig = _field_plot(vtu, vort, "RdBu_r",
                              f"{case_id} — vorticity",
                              r"$\omega = \partial v/\partial x - "
                              r"\partial u/\partial y$",
                              vmin=vmin, vmax=vmax)
            out = figure_path(figures_dir, case_id, "vorticity")
            fig.savefig(out)
            plt.close(fig)
            print(f"  wrote {os.path.relpath(out)}")
            n_figs += 1

    return n_figs > 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("case_dir", help="case result directory")
    parser.add_argument("case_id", nargs="?", default=None,
                        help="case id (default: basename of case_dir)")
    parser.add_argument("figures_dir", nargs="?",
                        default=os.path.join(os.path.dirname(__file__),
                                             "..", "report", "figures"))
    args = parser.parse_args()
    case_id = args.case_id or os.path.basename(os.path.normpath(args.case_dir))

    setup_figure()
    ensure_figures_dir(args.figures_dir)
    ok = plot_field(args.case_dir, case_id, args.figures_dir)
    # exit 2 signals "no figure produced" (missing data or unusable input);
    # the master script treats it as an informational skip, not a failure.
    sys.exit(0 if ok else 2)


if __name__ == "__main__":
    main()
