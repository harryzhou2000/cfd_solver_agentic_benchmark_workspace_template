"""Shared utilities for the CFD solver benchmark plotting scripts.

Handles reading of the solver output files (residuals.csv, forces.csv,
surface.csv, field_final.vtu) and publication-style matplotlib setup.
"""

import os
import xml.etree.ElementTree as ET

import numpy as np

# ---------------------------------------------------------------------------
# CSV reading
# ---------------------------------------------------------------------------


def read_csv(filepath):
    """Read a solver CSV file into a numpy structured array.

    The solver writes header lines such as::

        step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,residual_linf

    Columns are accessible by name, e.g. ``data["residual_l2"]``.  Columns
    that do not parse as numbers (e.g. the surface ``tag`` column) become
    string fields.  Robust to partially-written trailing rows (the solver
    may still be running while the file is read).  Returns None (with a
    warning printed) if the file is missing or contains no usable rows.
    """
    import csv as _csv

    if not os.path.isfile(filepath):
        print(f"  [warn] missing file: {filepath}")
        return None
    try:
        with open(filepath) as f:
            reader = _csv.reader(f)
            header = next(reader, None)
            if header is None:
                print(f"  [warn] empty file: {filepath}")
                return None
            names = [h.strip() for h in header]
            raw = []
            for row in reader:
                if len(row) != len(names):
                    continue  # partially written trailing row
                raw.append(row)
    except Exception as exc:  # pragma: no cover - defensive
        print(f"  [warn] could not parse {filepath}: {exc}")
        return None
    if not raw:
        print(f"  [warn] empty file: {filepath}")
        return None

    cols = list(zip(*raw))
    dtype = []
    arrays = []
    for name, col in zip(names, cols):
        try:
            arr = np.asarray([float(v) for v in col], dtype=float)
        except ValueError:
            arr = np.asarray(col, dtype="U")
        dtype.append((name, arr.dtype))
        arrays.append(arr)
    out = np.empty(len(raw), dtype=dtype)
    for name, arr in zip(names, arrays):
        out[name] = arr
    return out


# ---------------------------------------------------------------------------
# Publication-style matplotlib setup
# ---------------------------------------------------------------------------


def setup_figure():
    """Configure matplotlib rcParams for a publication-style look.

    Serif fonts, 10 pt base size, STIX math (LaTeX-like), light grid,
    high-DPI output.
    """
    import matplotlib

    matplotlib.rcParams.update({
        "font.family": "serif",
        "font.serif": ["Times New Roman", "Liberation Serif", "DejaVu Serif"],
        "font.size": 10,
        "mathtext.fontset": "stix",
        "axes.labelsize": 10,
        "axes.titlesize": 11,
        "axes.linewidth": 0.8,
        "axes.grid": True,
        "axes.axisbelow": True,
        "grid.color": "0.6",
        "grid.alpha": 0.35,
        "grid.linestyle": "--",
        "grid.linewidth": 0.6,
        "xtick.labelsize": 9,
        "ytick.labelsize": 9,
        "xtick.direction": "in",
        "ytick.direction": "in",
        "xtick.top": True,
        "ytick.right": True,
        "legend.fontsize": 9,
        "legend.frameon": True,
        "legend.framealpha": 0.9,
        "legend.edgecolor": "0.6",
        "lines.linewidth": 1.1,
        "lines.markersize": 3,
        "figure.dpi": 150,
        "savefig.dpi": 200,
        "savefig.bbox": "tight",
        "savefig.facecolor": "white",
    })


# ---------------------------------------------------------------------------
# VTU reading (ASCII XML UnstructuredGrid)
# ---------------------------------------------------------------------------


def _parse_numbers(text, dtype):
    """Parse a whitespace-separated number list from a DataArray element."""
    if not text or not text.strip():
        return np.zeros(0, dtype=dtype)
    return np.fromstring(text, sep=" ", dtype=dtype)


def read_vtu(filepath):
    """Parse an ASCII VTU (UnstructuredGrid) file.

    The solver writes 2D fields where points have 3 components (x, y, 0) and
    cells are TRI (type 5), QUAD (type 9) or POLYGON (type 7).

    Returns a dict with:
      - "points": (N, 3) float array
      - "cells":  list of numpy int arrays, one per cell, holding the point
                  indices of the cell's vertices
      - "cell_types": numpy int array of VTK cell type ids
      - "cell_data": dict name -> numpy array (per-cell, shape (M,) or (M, k))
      - "data_names": list of cell data array names in file order

    Returns None if the file is missing or cannot be parsed.
    """
    if not os.path.isfile(filepath):
        print(f"  [warn] missing file: {filepath}")
        return None
    try:
        tree = ET.parse(filepath)
    except Exception as exc:  # pragma: no cover - defensive
        print(f"  [warn] could not parse {filepath}: {exc}")
        return None

    root = tree.getroot()
    piece = root.find(".//UnstructuredGrid/Piece")
    if piece is None:
        piece = root.find(".//Piece")
    if piece is None:
        print(f"  [warn] no <Piece> in {filepath}")
        return None

    # ---- points -----------------------------------------------------------
    pts_el = piece.find("Points/DataArray")
    if pts_el is None:
        print(f"  [warn] no Points/DataArray in {filepath}")
        return None
    points = _parse_numbers(pts_el.text, np.float64)
    points = points.reshape(-1, 3)

    # ---- cells ------------------------------------------------------------
    conn = offs = types = None
    for da in piece.findall("Cells/DataArray"):
        name = (da.get("Name") or "").lower()
        if name == "connectivity":
            conn = _parse_numbers(da.text, np.int64)
        elif name == "offsets":
            offs = _parse_numbers(da.text, np.int64)
        elif name == "types":
            types = _parse_numbers(da.text, np.uint8)
    if conn is None or offs is None or types is None:
        print(f"  [warn] incomplete Cells section in {filepath}")
        return None

    cells = []
    start = 0
    for o in offs:
        cells.append(conn[start:o])
        start = o

    # ---- cell data --------------------------------------------------------
    cell_data = {}
    data_names = []
    for da in piece.findall("CellData/DataArray"):
        name = da.get("Name")
        if not name:
            continue
        comps = int(da.get("NumberOfComponents", "1"))
        vals = _parse_numbers(da.text, np.float64)
        vals = vals.reshape(-1, comps)
        cell_data[name] = vals
        data_names.append(name)

    return {
        "points": points,
        "cells": cells,
        "cell_types": types,
        "cell_data": cell_data,
        "data_names": data_names,
    }


# ---------------------------------------------------------------------------
# Mesh helpers
# ---------------------------------------------------------------------------


def cell_centroids(vtu):
    """Mean of each cell's vertex coordinates (cell-centered sampling point).

    The solver writes one copy of its vertices per cell, so the centroid is
    simply the mean of the cell's own points.
    """
    pts = vtu["points"]
    return np.array([pts[c].mean(axis=0) for c in vtu["cells"]])


def build_triangles(vtu):
    """Triangulate every cell polygon (TRI/QUAD/POLYGON) for tricontourf.

    Each cell contributes one value per generated triangle, so per-cell
    (cell-centered) data can be rendered directly with ``tricontourf``.
    Returns (triangles, cell_index_of_triangle).
    """
    triangles = []
    tri_cell = []
    for c, cell in enumerate(vtu["cells"]):
        nv = len(cell)
        if nv < 3:
            continue
        # fan triangulation of the (convex) cell polygon
        for k in range(1, nv - 1):
            triangles.append((cell[0], cell[k], cell[k + 1]))
            tri_cell.append(c)
    return np.asarray(triangles, dtype=np.int64), np.asarray(tri_cell, dtype=np.int64)


def cell_to_vertex(values, cells, points):
    """Map cell-centered data to vertex data by geometric-vertex averaging.

    The solver writes a private copy of its vertices for every cell, so each
    vertex belongs to exactly one cell and every geometric location appears
    once per adjacent cell.  Averaging the owning cells' values over all
    copies of the same (rounded) coordinate yields a continuous vertex field,
    suitable for ``tricontourf``/``tripcolor``.

    Parameters
    ----------
    values : (n,) cell-centered data (may be longer than len(cells); cells
             may reference a subset of indices)
    cells  : list of per-cell vertex index arrays
    points : (N, 3) vertex coordinates

    Returns
    -------
    vertex_values : (N,) array
    """
    n_pts = len(points)
    cell_of = np.empty(n_pts, dtype=np.int64)
    for c, vs in enumerate(cells):
        cell_of[vs] = c
    _, inv = np.unique(np.round(points[:, :2], 8), axis=0, return_inverse=True)
    sums = np.zeros(int(inv.max()) + 1)
    np.add.at(sums, inv, values[cell_of])
    counts = np.bincount(inv)
    avg = sums / np.maximum(counts, 1)
    return avg[inv]


def compute_vorticity(velocity, cells, centroids, points):
    """Cell-centered vorticity dv/dx - du/dy from adjacent-cell gradients.

    For each cell the velocity gradient is estimated by least squares from
    the cells sharing an edge.  Geometric adjacency is used: the solver
    writes a private copy of its vertices for every cell, so edge identity
    is based on (rounded) vertex coordinate pairs.  Cells with fewer than
    two neighbours receive zero vorticity.

    Parameters
    ----------
    velocity : (n, 2) array of cell-centered (u, v)
    cells    : list of per-cell vertex index arrays (from read_vtu)
    centroids: (n, 2) array of cell centroid coordinates
    points   : (N, 3) vertex coordinates (from read_vtu)

    Returns
    -------
    vorticity : (n,) array
    """
    n = len(cells)
    vort = np.zeros(n)
    if n == 0:
        return vort

    # Edge -> cell adjacency map.  Each edge is a sorted pair of rounded
    # vertex coordinates.
    edge_cells = {}
    for c, cell in enumerate(cells):
        nv = len(cell)
        for i in range(nv):
            a, b = int(cell[i]), int(cell[(i + 1) % nv])
            ka = tuple(np.round(points[a, :2], 8))
            kb = tuple(np.round(points[b, :2], 8))
            key = (ka, kb) if ka < kb else (kb, ka)
            edge_cells.setdefault(key, []).append(c)

    # Per-cell neighbour lists (cells sharing a full edge).
    adj = [[] for _ in range(n)]
    for cs in edge_cells.values():
        if len(cs) == 2:
            adj[cs[0]].append(cs[1])
            adj[cs[1]].append(cs[0])

    maxdeg = max((len(a) for a in adj), default=0)
    if maxdeg < 2:
        return vort

    # Vectorized least-squares velocity gradient per cell.
    pad = np.full((n, maxdeg), -1, dtype=np.int64)
    for c, nb in enumerate(adj):
        pad[c, : len(nb)] = nb

    valid = pad >= 0                      # (n, maxdeg) mask
    nb = np.where(valid, pad, 0)          # safe index array
    dx = centroids[nb, 0] - centroids[:, None, 0]
    dy = centroids[nb, 1] - centroids[:, None, 1]
    du = velocity[nb, 0] - velocity[:, None, 0]
    dv = velocity[nb, 1] - velocity[:, None, 1]

    sxx = np.sum(valid * dx * dx, axis=1)
    syy = np.sum(valid * dy * dy, axis=1)
    sxy = np.sum(valid * dx * dy, axis=1)
    sxdu = np.sum(valid * dx * du, axis=1)
    sydu = np.sum(valid * dy * du, axis=1)
    sxv = np.sum(valid * dx * dv, axis=1)
    syv = np.sum(valid * dy * dv, axis=1)

    det = sxx * syy - sxy * sxy
    ok = det > 1e-30
    with np.errstate(divide="ignore", invalid="ignore"):
        dudx = (syy * sxdu - sxy * sydu) / det
        dudy = (-sxy * sxdu + sxx * sydu) / det
        dvdx = (syy * sxv - sxy * syv) / det
        dvdy = (-sxy * sxv + sxx * syv) / det
    return np.where(ok, dvdx - dudy, 0.0)


# ---------------------------------------------------------------------------
# Case metadata helpers
# ---------------------------------------------------------------------------


def case_kind(case_id):
    """Classify a case id: 'cylinder' or 'naca' (or 'unknown')."""
    if "cylinder" in case_id:
        return "cylinder"
    if "naca" in case_id:
        return "naca"
    return "unknown"


def is_viscous(case_id):
    """True for laminar (no-slip wall) cases."""
    return "laminar" in case_id


def is_transient(case_id):
    """True for time-accurate cases (the Re200 cylinder)."""
    return "re200" in case_id


def figure_path(figures_dir, case_id, kind):
    """Absolute path of the output PNG for a case and figure kind."""
    return os.path.join(figures_dir, f"{case_id}_{kind}.png")


def ensure_figures_dir(figures_dir):
    os.makedirs(figures_dir, exist_ok=True)
    return figures_dir
