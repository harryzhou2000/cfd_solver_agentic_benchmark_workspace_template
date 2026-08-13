"""Field visualization: read a VTU file and generate contour plots.

Uses matplotlib tricontourf for unstructured data (no VTK dependency).

Usage:
    python plot_field.py --vtu solver/results/case/field_final.vtu \
                         --output-dir solver/report/figures/case \
                         --case-id naca0012_m015_inviscid
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import xml.etree.ElementTree as ET
from typing import Optional

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import rcParams
from matplotlib.tri import Triangulation

rcParams.update({
    "font.family": "serif",
    "font.serif": ["DejaVu Serif", "Times New Roman", "Liberation Serif"],
    "font.size": 11,
    "axes.titlesize": 13,
    "axes.labelsize": 12,
    "xtick.labelsize": 10,
    "ytick.labelsize": 10,
    "legend.fontsize": 9,
    "figure.dpi": 150,
    "savefig.dpi": 300,
    "savefig.bbox": "tight",
    "axes.grid": False,
})


def _parse_ascii_array(text, n_expected=None, dtype=float):
    """Parse a whitespace-separated ASCII data array."""
    vals = np.fromstring(text.strip(), sep=" ", dtype=dtype)
    if n_expected is not None and len(vals) != n_expected:
        # try whitespace split fallback
        vals = np.array(text.split(), dtype=dtype)
    return vals


def read_vtu(path):
    """Read an ASCII-format VTK XML UnstructuredGrid (.vtu) file.

    Returns (points, point_data, cells).
      points: (N, 2) float array of x,y coordinates
      point_data: dict of name -> 1-D array
    """
    tree = ET.parse(path)
    root = tree.getroot()
    piece = root.find(".//Piece")
    if piece is None:
        raise ValueError("No <Piece> found in VTU file")

    n_points = int(piece.get("NumberOfPoints", 0))
    n_cells = int(piece.get("NumberOfCells", 0))

    # Points
    pts_node = piece.find("Points/DataArray")
    pts_raw = pts_node.text.strip()
    pts = _parse_ascii_array(pts_raw, n_expected=n_points * 3, dtype=float)
    pts = pts.reshape(n_points, 3)
    points = pts[:, :2]  # x, y (ignore z in 2D)

    # Point data
    point_data = {}
    pd_node = piece.find("PointData")
    if pd_node is not None:
        for da in pd_node.findall("DataArray"):
            name = da.get("Name")
            fmt = da.get("format", "ascii")
            if fmt != "ascii":
                # binary / appended not supported here
                print(f"  [warn] DataArray '{name}' format={fmt}, skipping",
                      file=sys.stderr)
                continue
            vals = _parse_ascii_array(da.text.strip(), n_expected=n_points, dtype=float)
            if len(vals) == n_points:
                point_data[name] = vals
            else:
                print(f"  [warn] DataArray '{name}' has {len(vals)} values, "
                      f"expected {n_points}", file=sys.stderr)

    # Cells - connectivity for triangulation
    cells_node = piece.find("Cells")
    connectivity = None
    offsets = None
    cell_types = None
    if cells_node is not None:
        for da in cells_node.findall("DataArray"):
            name = da.get("Name")
            if name == "connectivity":
                connectivity = _parse_ascii_array(da.text.strip(), dtype=np.int32)
            elif name == "offsets":
                offsets = _parse_ascii_array(da.text.strip(), dtype=np.int32)
            elif name == "types":
                cell_types = _parse_ascii_array(da.text.strip(), dtype=np.uint8)

    return points, point_data, connectivity, offsets, cell_types


def _build_triangulation(points, connectivity, offsets):
    """Build a matplotlib Triangulation from VTU cell connectivity.

    For 2D unstructured grids the cells are typically triangles (VTK type 5)
    or quads (VTK type 9). We triangulate quads into two triangles.
    """
    if connectivity is None or offsets is None or len(offsets) == 0:
        # Fallback: use Delaunay
        return None

    # Determine cell sizes from offsets
    # offsets are cumulative, so cell i has size offsets[i] - offsets[i-1]
    n_cells = len(offsets)
    start = 0
    triangles = []
    for i in range(n_cells):
        end = int(offsets[i])
        cell_conn = connectivity[start:end]
        start = end
        nc = len(cell_conn)
        if nc == 3:
            triangles.append([int(c) for c in cell_conn])
        elif nc == 4:
            # quad -> two triangles
            a, b, c, d = [int(x) for x in cell_conn]
            triangles.append([a, b, c])
            triangles.append([a, c, d])
        # skip other element types

    if not triangles:
        return None

    triangles = np.array(triangles, dtype=np.int32)
    x = points[:, 0]
    y = points[:, 1]
    return Triangulation(x, y, triangles)


def _compute_vorticity(points, u, v, tri):
    """Compute vorticity = dv/dx - du/dy using matplotlib triangulation."""
    if u is None or v is None:
        return None
    # Use finite differences on the scattered points via a simple approach:
    # compute gradients using triangulation-based linear interpolation gradients.
    # A robust method: compute at each point using least-squares gradient
    # over neighbouring points.
    x = points[:, 0]
    y = points[:, 1]
    n = len(x)
    if tri is not None and hasattr(tri, "get_cpp_triangulator"):
        pass

    # Build neighbor lists from triangles
    neighbors = [set() for _ in range(n)]
    if tri is not None and tri.triangles is not None and len(tri.triangles) > 0:
        for tri_pts in tri.triangles:
            for ii in range(3):
                for jj in range(3):
                    if ii != jj:
                        neighbors[tri_pts[ii]].add(int(tri_pts[jj]))
    else:
        # fallback: simple nearest-neighbor
        pass

    dudy = np.zeros(n)
    dvdx = np.zeros(n)
    for i in range(n):
        nbrs = list(neighbors[i])
        if len(nbrs) < 3:
            continue
        dx = x[nbrs] - x[i]
        dy = y[nbrs] - y[i]
        du = u[nbrs] - u[i]
        dv = v[nbrs] - v[i]
        # Solve [dx dy] [dudx; dudy] = du  (least squares)
        A = np.column_stack([dx, dy])
        try:
            sol_u, *_ = np.linalg.lstsq(A, du, rcond=None)
            sol_v, *_ = np.linalg.lstsq(A, dv, rcond=None)
            dudy[i] = sol_u[1]
            dvdx[i] = sol_v[0]
        except np.linalg.LinAlgError:
            pass

    return dvdx - dudy


def _save(fig, out_dir, name):
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, name)
    fig.savefig(path)
    plt.close(fig)
    print(f"  [fig] {path}")
    return path


def _format_case_id(case_id):
    cid = case_id.lower()
    if cid.startswith("naca"):
        body = "NACA0012"
        rest = cid[len("naca0012"):]
        mach_str = ""
        if "_m" in rest:
            after = rest.split("_m", 1)[1]
            mach_token = after.split("_", 1)[0]
            try:
                mach_str = f"M={int(mach_token)/100:.2f}"
            except ValueError:
                mach_str = f"M={mach_token}"
        if "_inviscid" in rest:
            visc = "inviscid"
        elif "_laminar" in rest:
            visc = "laminar"
            if "_re" in rest:
                re_part = rest.split("_re", 1)[1]
                re_token = re_part.split("_", 1)[0] if "_" in re_part else re_part
                try:
                    visc = f"laminar, Re={int(re_token)}"
                except ValueError:
                    visc = f"laminar, Re={re_token}"
        else:
            visc = ""
        parts = [p for p in [body, mach_str, visc] if p]
        return ", ".join(parts)
    if cid.startswith("cylinder"):
        rest = cid[len("cylinder"):]
        mach_str = ""
        if "_m" in rest:
            after = rest.split("_m", 1)[1]
            mach_token = after.split("_", 1)[0]
            try:
                mach_str = f"M={int(mach_token)/100:.2f}"
            except ValueError:
                mach_str = f"M={mach_token}"
        visc = ""
        if "_laminar" in rest:
            if "_re" in rest:
                re_part = rest.split("_re", 1)[1]
                re_token = re_part.split("_", 1)[0] if "_" in re_part else re_part
                try:
                    visc = f"laminar, Re={int(re_token)}"
                except ValueError:
                    visc = f"laminar, Re={re_token}"
        parts = [p for p in ["Cylinder", mach_str, visc] if p]
        return ", ".join(parts)
    return case_id


def _case_type(case_id):
    cid = (case_id or "").lower()
    if "cylinder" in cid:
        return "cylinder"
    return "naca"


def _get_view(case_type):
    """Return (xlim, ylim) tuples for full-domain and zoom views."""
    if case_type == "naca":
        full = ((-0.2, 1.5), (-0.8, 0.8))
        zoom = ((-0.1, 1.1), (-0.2, 0.2))
        return [("full", full), ("zoom", zoom)]
    elif case_type == "cylinder":
        full = ((-5, 15), (-5, 5))
        zoom = ((-1.5, 8), (-3, 3))
        wake = ((-1.0, 15), (-3.0, 3.0))
        return [("full", full), ("zoom", zoom), ("wake", wake)]
    else:
        return [("full", None)]


def _contour_field(points, field, tri, out_dir, case_id, var_name, label,
                   case_type, levels=40, clim=None, cmap=None, xlim=None,
                   ylim=None, suffix=""):
    """Generate a filled tricontour plot for a single variable/view."""
    if field is None or len(field) == 0:
        print(f"  [skip] no data for {var_name}", file=sys.stderr)
        return None

    unit_labels = {
        "mach": "Mach number",
        "pressure": "Pressure",
        "density": r"Density",
        "velocity": "Velocity magnitude",
        "vorticity": "Vorticity",
        "temperature": "Temperature",
    }
    cb_label = unit_labels.get(var_name, var_name)

    if cmap is None:
        cmap = {
            "mach": "jet",
            "pressure": "viridis",
            "density": "plasma",
            "velocity": "magma",
            "vorticity": "RdBu_r",
            "temperature": "inferno",
        }.get(var_name, "viridis")

    x = points[:, 0]
    y = points[:, 1]

    fig, ax = plt.subplots(figsize=(9, 6))

    # Mask non-finite
    mask = np.isfinite(field)
    if not np.any(mask):
        plt.close(fig)
        return None

    xm, ym, fm = x[mask], y[mask], field[mask]
    if tri is not None and tri.triangles is not None and len(tri.triangles) > 0:
        # Use the triangulation we built (but we need to mask points too)
        # Recreate triangulation with only valid points
        try:
            t = Triangulation(xm, ym)
        except Exception:
            t = None
    else:
        try:
            t = Triangulation(xm, ym)
        except Exception:
            t = None

    if t is None:
        plt.close(fig)
        print(f"  [skip] triangulation failed for {var_name}", file=sys.stderr)
        return None

    try:
        if var_name == "vorticity":
            # Symmetric levels around zero
            vmax = np.percentile(np.abs(fm), 95)
            vmin = -vmax
            levels = np.linspace(vmin, vmax, levels)
            cs = ax.tricontourf(t, fm, levels=levels, cmap=cmap, extend="both")
        else:
            cs = ax.tricontourf(t, fm, levels=levels, cmap=cmap)
    except Exception as exc:
        plt.close(fig)
        print(f"  [skip] contour failed for {var_name}: {exc}", file=sys.stderr)
        return None

    if clim is not None:
        cs.set_clim(*clim)

    cbar = fig.colorbar(cs, ax=ax, pad=0.02)
    cbar.set_label(cb_label)

    ax.set_aspect("equal", adjustable="box")
    if xlim is not None:
        ax.set_xlim(*xlim)
    if ylim is not None:
        ax.set_ylim(*ylim)
    ax.set_xlabel("x")
    ax.set_ylabel("y")
    view_tag = f" ({suffix})" if suffix else ""
    ax.set_title(f"{cb_label}{view_tag} - {label}")
    fig.tight_layout()

    fname = f"{var_name}{('_' + suffix) if suffix else ''}.png"
    # Normalize: mach_full.png, mach_zoom.png etc.
    if suffix:
        fname = f"{var_name}_{suffix}.png"
    else:
        fname = f"{var_name}.png"
    return _save(fig, out_dir, fname)


def plot_field_file(vtu_path, out_dir, case_id="case", label=None):
    """Read a VTU and generate all field contour plots."""
    if not os.path.isfile(vtu_path):
        print(f"  [skip] VTU not found: {vtu_path}", file=sys.stderr)
        return []

    if label is None:
        label = _format_case_id(case_id)
    ct = _case_type(case_id)

    print(f"Reading VTU: {vtu_path}")
    points, point_data, conn, offsets, ctypes = read_vtu(vtu_path)
    print(f"  {len(points)} points, {len(point_data)} data arrays: "
          f"{list(point_data.keys())}")

    # Build triangulation from cell connectivity
    tri = _build_triangulation(points, conn, offsets)
    if tri is None:
        # fallback Delaunay
        print("  [info] building Delaunay triangulation fallback")
        tri = Triangulation(points[:, 0], points[:, 1])

    # Assemble variables to plot
    pd = point_data
    mach = pd.get("mach")
    pressure = pd.get("pressure")
    density = pd.get("density")
    u = pd.get("velocity_u")
    v = pd.get("velocity_v")
    temperature = pd.get("temperature")

    velocity_mag = None
    if u is not None and v is not None:
        velocity_mag = np.sqrt(u**2 + v**2)

    vorticity = _compute_vorticity(points, u, v, tri)

    variables = [
        ("mach", mach),
        ("pressure", pressure),
        ("density", density),
        ("velocity", velocity_mag),
        ("vorticity", vorticity),
    ]

    views = _get_view(ct)
    all_paths = []

    for var_name, field in variables:
        for view_name, (xlim, ylim) in views:
            # For cylinder wake zoom, only plot vorticity and velocity
            if view_name == "wake" and var_name not in ("vorticity", "velocity"):
                continue
            # For NACA, skip vorticity zoom (inviscid cases often lack meaningful vorticity)
            p = _contour_field(points, field, tri, out_dir, case_id, var_name,
                               label, ct, levels=40, xlim=xlim, ylim=ylim,
                               suffix=view_name if len(views) > 1 else "")
            if p:
                all_paths.append(p)

    return all_paths


def main():
    ap = argparse.ArgumentParser(description="Generate field contour plots from a VTU file")
    ap.add_argument("--vtu", required=True, help="Path to field_final.vtu")
    ap.add_argument("--output-dir", required=True, help="Output directory for PNGs")
    ap.add_argument("--case-id", default="case", help="Case identifier (e.g. naca0012_m015_inviscid)")
    ap.add_argument("--label", default=None, help="Display label (overrides case-id formatting)")
    args = ap.parse_args()

    paths = plot_field_file(args.vtu, args.output_dir, args.case_id, args.label)
    print(f"\n  Generated {len(paths)} field figures.")
    return 0 if paths else 1


if __name__ == "__main__":
    sys.exit(main())
