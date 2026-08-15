#!/usr/bin/env python3
"""Generate publication-quality plots from CFD solver output files.

Reads the solver outputs described in OUTPUT_CONTRACT.md (residuals.csv,
forces.csv, surface.csv, field_final.pvtu + per-rank .vtu pieces) and writes
PNG figures into the report figures directory. VTU files are parsed with the
standard library only (no vtk/pyvista dependency).

Usage:
    python3 tools/plot_results.py \
        --case-dir results/naca0012_m015_inviscid/ \
        --output-dir report/figures/ \
        --case-id naca0012_m015_inviscid \
        [--all | --residuals | --forces | --surface | --contours]
"""

import argparse
import json
import math
import os
import sys
import xml.etree.ElementTree as ET

import numpy as np
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.tri import Triangulation

# Settings for scholarly style
plt.rcParams.update(
    {
        "font.size": 11,
        "font.family": "serif",
        "axes.labelsize": 12,
        "axes.titlesize": 13,
        "legend.fontsize": 10,
        "figure.dpi": 150,
        "savefig.bbox": "tight",
        "savefig.pad_inches": 0.05,
    }
)


# ---------------------------------------------------------------------------
# CSV readers
# ---------------------------------------------------------------------------

def _read_csv(path):
    """Read a CSV file into a dict of column name -> np.ndarray."""
    if not os.path.exists(path):
        raise FileNotFoundError(f"missing {path}")
    with open(path) as f:
        header = f.readline().strip().split(",")
        cols = {name: [] for name in header}
        for line in f:
            parts = line.strip().split(",")
            for i, name in enumerate(header):
                try:
                    cols[name].append(float(parts[i]))
                except (ValueError, IndexError):
                    cols[name].append(np.nan)
    return {name: np.asarray(v) for name, v in cols.items()}


def _load_metadata(case_dir):
    """Return metadata.json as a dict (empty when absent)."""
    path = os.path.join(case_dir, "metadata.json")
    if os.path.exists(path):
        with open(path) as f:
            return json.load(f)
    return {}


# ---------------------------------------------------------------------------
# VTU readers (unstructured grid, ASCII)
# ---------------------------------------------------------------------------

def _read_vtu(path):
    """Parse one VTU piece.

    Returns dict with keys: points (Nx2), cells (list of vertex index lists),
    cell_types (np.ndarray), data (dict name -> np.ndarray per cell).
    """
    root = ET.parse(path).getroot()
    piece = root.find("UnstructuredGrid/Piece")
    if piece is None:
        raise ValueError(f"{path}: no UnstructuredGrid/Piece")
    n_points = int(piece.get("NumberOfPoints"))

    def data_array(parent, name=None, array_name=None):
        for da in parent.iter("DataArray"):
            if array_name is not None and da.get("Name") != array_name:
                continue
            if name is not None and da.get("Name") != name:
                continue
            return np.fromstring(da.text, sep=" ")
        return None

    # Points
    points_da = None
    for da in piece.find("Points").iter("DataArray"):
        points_da = da
        break
    raw = np.fromstring(points_da.text, sep=" ")
    ncomp = int(points_da.get("NumberOfComponents", 3))
    points = raw.reshape(-1, ncomp)[:, :2]

    # Cells
    cells_el = piece.find("Cells")
    conn = data_array(cells_el, array_name="connectivity").astype(np.int64)
    offsets = data_array(cells_el, array_name="offsets").astype(np.int64)
    types = data_array(cells_el, array_name="types").astype(np.uint8)
    cells = []
    start = 0
    for off in offsets:
        cells.append(conn[start:off].tolist())
        start = off

    # CellData
    data = {}
    celldata = piece.find("CellData")
    if celldata is not None:
        for da in celldata.iter("DataArray"):
            name = da.get("Name")
            arr = np.fromstring(da.text, sep=" ")
            data[name] = arr

    return {
        "points": points,
        "cells": cells,
        "cell_types": types,
        "data": data,
        "n_points": n_points,
    }


def read_field(case_dir):
    """Read the final field: merge all rank pieces listed in the .pvtu."""
    pvtu = os.path.join(case_dir, "field_final.pvtu")
    if not os.path.exists(pvtu):
        pvtu = os.path.join(case_dir, "field_final.vtu")
    if not os.path.exists(pvtu):
        raise FileNotFoundError(f"no field_final.* in {case_dir}")

    if pvtu.endswith(".pvtu"):
        root = ET.parse(pvtu).getroot()
        sources = [p.get("Source") for p in root.iter("Piece")]
    else:
        sources = [os.path.basename(pvtu)]

    merged = {"points": [], "cells": [], "cell_types": [], "data": {}}
    offset = 0
    for src in sources:
        piece_path = os.path.join(case_dir, os.path.basename(src))
        if not os.path.exists(piece_path):
            piece_path = os.path.join(case_dir, src)
        piece = _read_vtu(piece_path)
        merged["points"].append(piece["points"])
        merged["cells"].append(
            [[v + offset for v in cell] for cell in piece["cells"]]
        )
        merged["cell_types"].append(piece["cell_types"])
        for name, arr in piece["data"].items():
            merged["data"].setdefault(name, []).append(arr)
        offset += piece["n_points"]

    points = np.vstack(merged["points"])
    cells = [c for group in merged["cells"] for c in group]
    types = np.concatenate(merged["cell_types"])
    data = {name: np.concatenate(arrs) for name, arrs in merged["data"].items()}
    return {"points": points, "cells": cells, "cell_types": types, "data": data}


def triangulate(field):
    """Build a Triangulation (triangles only; quads split into two)."""
    points = field["points"]
    triangles = []
    cell_of_tri = []
    for i, (cell, ctype) in enumerate(zip(field["cells"], field["cell_types"])):
        if ctype == 5:  # triangle
            triangles.append(cell[:3])
            cell_of_tri.append(i)
        elif ctype == 9:  # quad -> two triangles
            a, b, c, d = cell[:4]
            triangles.append([a, b, c])
            triangles.append([a, c, d])
            cell_of_tri.append(i)
            cell_of_tri.append(i)
        else:
            raise ValueError(f"unsupported VTK cell type {ctype}")
    tri = Triangulation(points[:, 0], points[:, 1], np.asarray(triangles))
    return tri, np.asarray(cell_of_tri)


def cell_to_triangle(values, cell_of_tri):
    """Expand per-cell values to per-triangle values."""
    return np.asarray(values)[cell_of_tri]


def compute_vorticity(field):
    """Cell-centered vorticity dv/dx - du/dy from vertex-averaged velocities.

    Vertex velocities are averaged from adjacent cell centers; the linear
    gradient is fitted through the first three vertices of each cell.
    """
    points = field["points"]
    u = field["data"]["velocity_x"]
    v = field["data"]["velocity_y"]
    n = len(points)
    u_vert = np.zeros(n)
    v_vert = np.zeros(n)
    count = np.zeros(n)
    for cell in field["cells"]:
        for vidx in cell:
            count[vidx] += 1
    for ci, cell in enumerate(field["cells"]):
        for vidx in cell:
            u_vert[vidx] += u[ci]
            v_vert[vidx] += v[ci]
    u_vert /= np.maximum(count, 1)
    v_vert /= np.maximum(count, 1)

    vort = np.zeros(len(field["cells"]))
    for ci, cell in enumerate(field["cells"]):
        i0, i1, i2 = cell[:3]
        dx1 = points[i1, 0] - points[i0, 0]
        dy1 = points[i1, 1] - points[i0, 1]
        dx2 = points[i2, 0] - points[i0, 0]
        dy2 = points[i2, 1] - points[i0, 1]
        det = dx1 * dy2 - dx2 * dy1
        if abs(det) < 1e-30:
            continue
        du1 = u_vert[i1] - u_vert[i0]
        du2 = u_vert[i2] - u_vert[i0]
        dv1 = v_vert[i1] - v_vert[i0]
        dv2 = v_vert[i2] - v_vert[i0]
        du_dx = (du1 * dy2 - du2 * dy1) / det
        du_dy = (dx1 * du2 - dx2 * du1) / det
        dv_dx = (dv1 * dy2 - dv2 * dy1) / det
        dv_dy = (dx1 * dv2 - dx2 * dv1) / det
        vort[ci] = dv_dx - du_dy
    return vort


# ---------------------------------------------------------------------------
# Plot helpers
# ---------------------------------------------------------------------------

def _save(fig, output_dir, name):
    os.makedirs(output_dir, exist_ok=True)
    path = os.path.join(output_dir, name)
    fig.savefig(path)
    plt.close(fig)
    print(f"wrote {path}")
    return name


def _grid(ax, yscale=None):
    ax.grid(True, which="both", alpha=0.3)
    ax.set_axisbelow(True)
    if yscale:
        ax.set_yscale(yscale)


def _surface_axis(case_id, rows):
    """Return (x_values, xlabel) for surface plots.

    NACA cases use x; cylinder cases use theta measured from the upstream
    stagnation point (direction from the cylinder center toward (-1, 0)).
    """
    x = rows["x"]
    y = rows["y"]
    span = float(x.max() - x.min())
    aspect = float((y.max() - y.min()) / span) if span > 0 else 0
    if "cylinder" in case_id.lower() or aspect < 0.5:
        xc = (x.min() + x.max()) / 2.0
        yc = (y.min() + y.max()) / 2.0
        lead = math.atan2(0.0 - yc, -1.0 - xc)
        theta = np.angle(np.exp(1j * (np.arctan2(y - yc, x - xc) - lead)))
        theta = np.mod(theta, 2.0 * np.pi)
        return theta, r"$\theta$ (from upstream stagnation point) [rad]"
    return x, "x"


# ---------------------------------------------------------------------------
# Individual plots
# ---------------------------------------------------------------------------

def plot_residual_history(case_dir, output_dir, case_id):
    res = _read_csv(os.path.join(case_dir, "residuals.csv"))
    fig, ax = plt.subplots(figsize=(7, 4.2))
    step = res["step"]
    ax.plot(step, res["rho"], lw=1.2, label=r"$\rho$")
    ax.plot(step, res["rhou"], lw=1.2, label=r"$\rho u$")
    ax.plot(step, res["rhov"], lw=1.2, label=r"$\rho v$")
    ax.plot(step, res["rhoE"], lw=1.2, label=r"$\rho E$")
    ax.plot(step, res["residual_l2"], lw=1.8, color="k", label=r"aggregate $L_2$")
    _grid(ax, yscale="log")
    ax.set_xlabel("iteration step")
    ax.set_ylabel(r"residual (global $L_2$ norm)")
    ax.set_title(f"{case_id}: residual history")
    ax.legend(ncol=2, frameon=False)
    return _save(fig, output_dir, f"{case_id}_residuals.png")


def plot_force_history(case_dir, output_dir, case_id):
    forces = _read_csv(os.path.join(case_dir, "forces.csv"))
    meta = _load_metadata(case_dir)
    x = forces["step"]
    xlabel = "iteration step"
    if meta.get("time_integrator") == "bdf2" or forces["physical_time"].max() > 0:
        x = forces["physical_time"]
        xlabel = "physical time"
    fig, ax = plt.subplots(figsize=(7, 4.2))
    ax.plot(x, forces["cd"], lw=1.6, color="tab:red", label=r"$C_D$")
    ax.plot(x, forces["cl"], lw=1.6, color="tab:blue", label=r"$C_L$")
    _grid(ax)
    ax.set_xlabel(xlabel)
    ax.set_ylabel("force coefficient")
    ax.set_title(f"{case_id}: force history")
    ax.legend(frameon=False)
    return _save(fig, output_dir, f"{case_id}_forces.png")


def plot_surface_cp(case_dir, output_dir, case_id):
    surf = _read_csv(os.path.join(case_dir, "surface.csv"))
    xs, xlabel = _surface_axis(case_id, surf)
    cp = surf["cp"]
    fig, ax = plt.subplots(figsize=(7, 4.2))
    if "naca" in case_id.lower():
        # upper surface (y>0) and lower surface (y<0)
        upper = surf["y"] > 0
        ax.plot(xs[upper], cp[upper], "o", ms=2.5, color="tab:red",
                label="upper surface")
        ax.plot(xs[~upper], cp[~upper], "s", ms=2.5, color="tab:blue",
                label="lower surface")
        ax.invert_yaxis()
    else:
        ax.plot(xs, cp, ".", ms=3, color="tab:red")
    _grid(ax)
    ax.set_xlabel(xlabel)
    ax.set_ylabel(r"pressure coefficient $C_p$")
    ax.set_title(f"{case_id}: surface pressure distribution")
    if "naca" in case_id.lower():
        ax.legend(frameon=False, loc="best")
    return _save(fig, output_dir, f"{case_id}_cp.png")


def plot_surface_cf(case_dir, output_dir, case_id):
    surf = _read_csv(os.path.join(case_dir, "surface.csv"))
    xs, xlabel = _surface_axis(case_id, surf)
    cf = surf["cf"]
    if np.max(np.abs(cf)) <= 0.0:
        print(f"note: {case_id}: skin friction is identically zero "
              "(inviscid case) — skipping cf figure")
        return None
    fig, ax = plt.subplots(figsize=(7, 4.2))
    if "naca" in case_id.lower():
        upper = surf["y"] > 0
        ax.plot(xs[upper], cf[upper], "o", ms=2.5, color="tab:red",
                label="upper surface")
        ax.plot(xs[~upper], cf[~upper], "s", ms=2.5, color="tab:blue",
                label="lower surface")
        ax.legend(frameon=False)
    else:
        ax.plot(xs, cf, ".", ms=3, color="tab:red")
    _grid(ax)
    ax.set_xlabel(xlabel)
    ax.set_ylabel(r"skin-friction coefficient $C_f$")
    ax.set_title(f"{case_id}: surface skin-friction distribution")
    return _save(fig, output_dir, f"{case_id}_cf.png")


def plot_field_contour(case_dir, field_variable, output_dir, case_id,
                       zoom=None, cmap="viridis", vrange=None, name=None,
                       title=None, levels=60):
    """Filled contour of a cell-centered field variable."""
    field = read_field(case_dir)
    tri, cell_of_tri = triangulate(field)
    if field_variable == "vorticity":
        values = compute_vorticity(field)
    else:
        values = field["data"][field_variable]
    face = cell_to_triangle(values, cell_of_tri)
    if vrange is not None:
        vmin, vmax = vrange
    else:
        vmin, vmax = np.percentile(face, 0.5), np.percentile(face, 99.5)

    fig, ax = plt.subplots(figsize=(8.2, 4.6))
    tc = ax.tripcolor(tri, facecolors=face, cmap=cmap,
                      vmin=vmin, vmax=vmax, shading="flat")
    cb = fig.colorbar(tc, ax=ax, fraction=0.035, pad=0.02)
    cb.set_label(field_variable)

    x = field["points"][:, 0]
    y = field["points"][:, 1]
    if zoom is None:
        ax.set_xlim(x.min(), x.max())
        ax.set_ylim(y.min(), y.max())
    elif zoom == "body":
        # near-body zoom: central band around the body
        xmin, xmax = x.min(), x.max()
        ymin, ymax = y.min(), y.max()
        ax.set_xlim(xmin, xmin + 0.5 * (xmax - xmin))
        ax.set_ylim(ymin + 0.25 * (ymax - ymin), ymin + 0.75 * (ymax - ymin))
    else:
        x0, x1, y0, y1 = zoom
        ax.set_xlim(x0, x1)
        ax.set_ylim(y0, y1)
    ax.set_aspect("equal")
    ax.set_xlabel("x")
    ax.set_ylabel("y")
    ax.set_title(title or f"{case_id}: {field_variable} field")
    fname = name or f"{case_id}_{field_variable}.png"
    return _save(fig, output_dir, fname)


def plot_mach_contour(case_dir, output_dir, case_id, zoom=None, name=None):
    return plot_field_contour(
        case_dir, "mach", output_dir, case_id, zoom=zoom, cmap="plasma",
        name=name, title=f"{case_id}: Mach number field")


def plot_pressure_contour(case_dir, output_dir, case_id, zoom=None, name=None):
    return plot_field_contour(
        case_dir, "pressure", output_dir, case_id, zoom=zoom, cmap="cividis",
        name=name, title=f"{case_id}: pressure field")


def plot_wake_vorticity(case_dir, output_dir, case_id, clip_range=(-5.0, 5.0)):
    return plot_field_contour(
        case_dir, "vorticity", output_dir, case_id, cmap="RdBu_r",
        vrange=clip_range, name=f"{case_id}_vorticity.png",
        title=f"{case_id}: vorticity field (clipped $[{clip_range[0]}, "
              f"{clip_range[1]}]$)")


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case-dir", required=True,
                        help="solver output directory for one case")
    parser.add_argument("--output-dir", default="report/figures",
                        help="where PNG figures are written")
    parser.add_argument("--case-id", default=None,
                        help="case id (defaults to the output-dir basename)")
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--all", action="store_true", default=True,
                       help="generate every plot type (default)")
    group.add_argument("--residuals", action="store_true")
    group.add_argument("--forces", action="store_true")
    group.add_argument("--surface", action="store_true")
    group.add_argument("--contours", action="store_true")
    args = parser.parse_args()

    # An explicit --case-id is authoritative (needed for repeated runs of the
    # same case, e.g. the np=2/4/8 NACA runs); otherwise fall back to the
    # metadata case id, then to the output-dir basename.
    case_id = args.case_id
    if case_id is None:
        meta = _load_metadata(args.case_dir)
        case_id = str(meta.get("case_id", os.path.basename(
            os.path.normpath(args.case_dir))))
    meta = _load_metadata(args.case_dir)
    viscous = str(meta.get("viscous_flux", "")) == "gradient_based"
    transient = str(meta.get("time_integrator", "")) == "bdf2"

    made = []
    if args.all or args.residuals:
        made.append(plot_residual_history(args.case_dir, args.output_dir, case_id))
    if args.all or args.forces:
        made.append(plot_force_history(args.case_dir, args.output_dir, case_id))
    if args.all or args.surface:
        made.append(plot_surface_cp(args.case_dir, args.output_dir, case_id))
        if viscous:
            cf = plot_surface_cf(args.case_dir, args.output_dir, case_id)
            if cf:
                made.append(cf)
    if args.all or args.contours:
        made.append(plot_mach_contour(args.case_dir, args.output_dir, case_id))
        made.append(plot_pressure_contour(args.case_dir, args.output_dir, case_id))
        if "naca" in case_id.lower():
            made.append(plot_mach_contour(args.case_dir, args.output_dir,
                                          case_id, zoom="body",
                                          name=f"{case_id}_mach_zoom.png"))
            made.append(plot_pressure_contour(args.case_dir, args.output_dir,
                                              case_id, zoom="body",
                                              name=f"{case_id}_pressure_zoom.png"))
        if "cylinder" in case_id.lower():
            made.append(plot_wake_vorticity(args.case_dir, args.output_dir,
                                            case_id))
    print(f"\n{len(made)} figures generated for {case_id}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
