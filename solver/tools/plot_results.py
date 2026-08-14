#!/usr/bin/env python3
"""Generate all report figures for the CFD benchmark.

Reads solver output CSVs (and optionally VTK/VTU field files) from
``solver/results/<case_name>/`` and writes figures to
``solver/report/figures/<case_name>_<plot_type>.png``, plus
``solver/report/figure_manifest.csv`` and ``solver/report/sanity_checks.json``.

Output contract (cfd_solver_agentic_benchmark/OUTPUT_CONTRACT.md):
  residuals.csv : step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,residual_linf
  forces.csv    : step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,viscous_lift
  surface.csv   : x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag
  field_final.vtk / .vtu : unstructured grid with cell data (density, pressure, mach, velocity)

Figures per case:
  <case>_residuals.png   step vs residual_l2 (log scale)
  <case>_forces.png      step vs cl, cd
  <case>_surface_cp.png  surface pressure coefficient (NACA: x vs cp; cylinder: angle vs cp)
  <case>_mach.png        Mach field (VTK filled contours, or surface fallback)
  <case>_pressure.png    Pressure field (VTK filled contours, or surface fallback)

Dependencies: numpy, matplotlib (both available in solver/.venv).
Pure-Python legacy .vtk and XML .vtu readers are included so no VTK/PyVista
install is required.
"""

import csv
import json
import os
import sys
import warnings
from datetime import datetime, timezone
from pathlib import Path

import numpy as np
import matplotlib

matplotlib.use("Agg")  # headless-safe
import matplotlib.pyplot as plt
import matplotlib.tri as mtri

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
SOLVER_ROOT = Path(__file__).resolve().parents[1]
RESULTS_DIR = SOLVER_ROOT / "results"
REPORT_DIR = SOLVER_ROOT / "report"
FIGURES_DIR = REPORT_DIR / "figures"
MANIFEST_PATH = REPORT_DIR / "figure_manifest.csv"
SANITY_PATH = REPORT_DIR / "sanity_checks.json"

GAMMA_DEFAULT = 1.4  # used to reconstruct pressure if surface.csv is absent

# ---------------------------------------------------------------------------
# Scholarly styling
# ---------------------------------------------------------------------------
plt.rcParams.update(
    {
        "font.family": "serif",
        "font.size": 11,
        "axes.labelsize": 12,
        "axes.titlesize": 13,
        "legend.fontsize": 10,
        "axes.grid": True,
        "grid.alpha": 0.3,
        "grid.linestyle": "--",
        "lines.linewidth": 1.5,
        "figure.dpi": 150,
        "savefig.bbox": "tight",
        "axes.linewidth": 0.8,
    }
)

FIGURE_DPI = 150


def style_axes(ax, title, xlabel, ylabel):
    """Apply consistent scholarly axis styling."""
    ax.set_title(title)
    ax.set_xlabel(xlabel)
    ax.set_ylabel(ylabel)
    ax.grid(True, which="both", alpha=0.3, linestyle="--")
    ax.tick_params(which="both", direction="in", top=True, right=True)


# ---------------------------------------------------------------------------
# CSV helpers
# ---------------------------------------------------------------------------
def read_csv(path):
    """Read a CSV into a dict of {column_name: np.ndarray}. Returns {} on error."""
    try:
        with open(path, newline="") as f:
            reader = csv.DictReader(f)
            if reader.fieldnames is None:
                return {}
            cols = {name: [] for name in reader.fieldnames}
            for row in reader:
                for name in reader.fieldnames:
                    cols[name].append(row.get(name, ""))
        result = {}
        for name, values in cols.items():
            try:
                result[name] = np.array(values, dtype=float)
            except ValueError:
                result[name] = np.array(values, dtype=str)
        return result
    except (OSError, csv.Error) as exc:
        warnings.warn(f"Could not read {path}: {exc}")
        return {}


def require_cols(data, cols, case, filename):
    """Return the named columns, or warn and return None."""
    missing = [c for c in cols if c not in data]
    if missing:
        warnings.warn(
            f"[{case}] {filename} missing column(s) {missing}; skipping figure"
        )
        return None
    return tuple(data[c] for c in cols)


# ---------------------------------------------------------------------------
# VTK / VTU readers (pure Python, no vtk/pyvista dependency)
# ---------------------------------------------------------------------------
def _parse_legacy_vtk(path):
    """Parse a legacy ASCII .vtk unstructured grid.

    Returns dict with keys: points (N,3), cells (list of int lists),
    cell_types (list), cell_data (dict name->array), point_data (dict).
    """
    with open(path, "r", errors="replace") as f:
        lines = f.readlines()
    result = {
        "points": None,
        "cells": [],
        "cell_types": [],
        "cell_data": {},
        "point_data": {},
    }
    i = 0
    n = len(lines)

    def tokens(ln):
        return ln.split()

    while i < n:
        ln = lines[i].strip()
        if not ln or ln.startswith("#"):
            i += 1
            continue
        parts = tokens(ln)
        key = parts[0].upper()
        if key == "POINTS":
            count = int(parts[1])
            vals = []
            while len(vals) < count * 3:
                i += 1
                vals.extend(float(v) for v in tokens(lines[i]))
            result["points"] = np.array(vals[: count * 3]).reshape(count, 3)
        elif key == "CELLS":
            count = int(parts[1])
            cells = []
            consumed = 0
            while consumed < count:
                i += 1
                p = tokens(lines[i])
                k = int(p[0])
                cells.append([int(v) for v in p[1 : 1 + k]])
                consumed += 1
            result["cells"] = cells
        elif key == "CELL_TYPES":
            count = int(parts[1])
            types = []
            while len(types) < count:
                i += 1
                types.extend(int(v) for v in tokens(lines[i]))
            result["cell_types"] = types[:count]
        elif key in ("CELL_DATA", "POINT_DATA"):
            count = int(parts[1])
            target = result["cell_data"] if key == "CELL_DATA" else result["point_data"]
            # Scan following SCALARS / VECTORS blocks until next section keyword.
            i += 1
            while i < n:
                ln2 = lines[i].strip()
                if not ln2:
                    i += 1
                    continue
                p2 = tokens(ln2)
                k2 = p2[0].upper()
                if k2 in ("POINTS", "CELLS", "CELL_TYPES", "CELL_DATA", "POINT_DATA", "DATASET"):
                    break
                if k2 == "SCALARS":
                    name = p2[1]
                    i += 1
                    # optional LOOKUP_TABLE line
                    if i < n and tokens(lines[i])[0].upper() == "LOOKUP_TABLE":
                        i += 1
                    vals = []
                    while len(vals) < count:
                        vals.extend(float(v) for v in tokens(lines[i]))
                        i += 1
                    target[name] = np.array(vals[:count])
                elif k2 == "VECTORS":
                    name = p2[1]
                    vals = []
                    while len(vals) < count * 3:
                        i += 1
                        vals.extend(float(v) for v in tokens(lines[i]))
                    target[name] = np.array(vals[: count * 3]).reshape(count, 3)
                else:
                    i += 1
            continue
        i += 1
    return result


def _parse_vtu(path):
    """Parse a minimal XML .vtu (unstructured grid, ASCII, one piece)."""
    import xml.etree.ElementTree as ET

    tree = ET.parse(path)
    root = tree.getroot()
    ugrid = root.find(".//UnstructuredGrid")
    if ugrid is None:
        raise ValueError("not an UnstructuredGrid VTU")
    piece = ugrid.find("Piece")
    if piece is None:
        raise ValueError("VTU has no <Piece>")
    npts = int(piece.get("NumberOfPoints", 0))
    ncells = int(piece.get("NumberOfCells", 0))

    def data_arrays(parent):
        arrays = {}
        for da in parent.findall("DataArray"):
            name = da.get("Name")
            fmt = da.get("format", "ascii").lower()
            text = (da.text or "").strip()
            if fmt == "ascii":
                vals = np.array(text.split(), dtype=float)
            elif fmt == "binary":
                raise ValueError(f"binary VTU DataArray '{name}' not supported")
            else:
                vals = np.array(da.get("values", "").split(), dtype=float)
            arrays[name] = vals
        return arrays

    points_el = piece.find("Points")
    points_arrays = data_arrays(points_el) if points_el is not None else {}
    pts = None
    for name in ("Points", "points"):
        if name in points_arrays:
            pts = points_arrays[name].reshape(-1, 3)
            break
    if pts is None and points_arrays:
        pts = next(iter(points_arrays.values())).reshape(-1, 3)

    cells_el = piece.find("Cells")
    cells_arrays = data_arrays(cells_el) if cells_el is not None else {}
    conn = cells_arrays.get("connectivity")
    offsets = cells_arrays.get("offsets")
    types = cells_arrays.get("types")
    cells = []
    cell_types = []
    if conn is not None:
        if offsets is None:
            offsets = np.arange(len(conn)) + 1
        offsets = offsets.astype(int)
        start = 0
        for off in offsets:
            cells.append([int(v) for v in conn[start:off]])
            start = off
    if types is not None:
        cell_types = [int(t) for t in types]

    cell_data, point_data = {}, {}
    for section, target in (("CellData", cell_data), ("PointData", point_data)):
        el = piece.find(section)
        if el is not None:
            target.update(data_arrays(el))

    return {
        "points": pts,
        "cells": cells,
        "cell_types": cell_types,
        "cell_data": cell_data,
        "point_data": point_data,
    }


def read_field_file(path):
    """Read a VTK/VTU field file into the common dict layout. Returns None on failure."""
    try:
        if path.suffix.lower() == ".vtu":
            return _parse_vtu(path)
        if path.suffix.lower() == ".vtk":
            return _parse_legacy_vtk(path)
        return None
    except Exception as exc:
        warnings.warn(f"Could not parse field file {path}: {exc}")
        return None


def build_field_triangulation(field):
    """Build (x, y, values, Triangulation) from a parsed field file.

    Uses real cell connectivity when it is valid (triangle/quad/tet cells);
    otherwise falls back to a Delaunay triangulation of cell centroids.
    Returns None if no spatial layout is recoverable.
    """
    pts = field.get("points")
    cells = field.get("cells") or []
    if pts is None or len(pts) == 0:
        return None
    cell_types = field.get("cell_types") or []
    npts = len(pts)

    triangles = []
    for idx, cell in enumerate(cells):
        ctype = cell_types[idx] if idx < len(cell_types) else 5
        verts = [int(v) for v in cell]
        if ctype == 5:  # triangle
            tri = verts[:3]
        elif ctype == 9:  # quad -> split
            if len(verts) >= 4:
                tri = [verts[0], verts[1], verts[2], verts[0], verts[2], verts[3]]
            else:
                continue
        elif ctype == 10:  # tetra -> use base face
            tri = verts[:3]
        else:
            continue
        if all(0 <= v < npts for v in tri):
            triangles.extend(tri)

    if len(triangles) >= 9 and len(set(zip(*[iter(triangles)] * 3))) >= 2:
        x = pts[:, 0]
        y = pts[:, 1]
        tri_obj = mtri.Triangulation(x, y, np.array(triangles).reshape(-1, 3))
        # prefer point data; fall back to cell data averaged onto nodes
        return {"tri": tri_obj, "cell_based": False}

    # Fallback: Delaunay of cell centroids (cell data). Degenerate (all-identical)
    # connectivity yields coincident centroids, which Delaunay cannot handle.
    centroids = []
    for cell in cells:
        verts = [int(v) for v in cell]
        if not verts or not all(0 <= v < npts for v in verts):
            continue
        centroids.append(pts[verts].mean(axis=0))
    if len(centroids) < 4:
        return None
    cx = np.array([c[0] for c in centroids])
    cy = np.array([c[1] for c in centroids])
    if np.ptp(cx) < 1e-12 or np.ptp(cy) < 1e-12:
        return None  # degenerate layout (e.g. placeholder connectivity)
    try:
        tri_obj = mtri.Triangulation(cx, cy)
        return {"tri": tri_obj, "cell_based": True}
    except Exception as exc:
        warnings.warn(f"Triangulation failed: {exc}")
        return None


def field_values(field, layout, name):
    """Extract scalar values for a named variable from the parsed field."""
    if name in field.get("point_data", {}):
        return field["point_data"][name]
    if name in field.get("cell_data", {}):
        if not layout.get("cell_based"):
            # cell data on a node triangulation: use node-averaged values
            vals = field["cell_data"][name]
            tri = layout["tri"]
            node_vals = np.zeros(len(tri.x))
            counts = np.zeros(len(tri.x))
            ncells = len(tri.triangles)
            cell_vals = vals if len(vals) >= ncells else np.resize(vals, ncells)
            for t, cv in zip(tri.triangles, cell_vals):
                for v in t:
                    node_vals[v] += cv
                    counts[v] += 1
            counts[counts == 0] = 1
            return node_vals / counts
        return field["cell_data"][name]
    return None


# ---------------------------------------------------------------------------
# Figure generators
# ---------------------------------------------------------------------------
def plot_field_generator(varname, title_var, cmap):
    """Return a generator callable plotting `varname` from the case field file."""

    def _gen(case_dir, case_name):
        return plot_field(case_dir, case_name, varname, title_var, cmap)

    return _gen


def plot_residuals(case_dir, case_name):
    data = read_csv(case_dir / "residuals.csv")
    cols = require_cols(data, ["step", "residual_l2"], case_name, "residuals.csv")
    if cols is None:
        return None
    step, l2 = cols
    fig, ax = plt.subplots(figsize=(7, 4.5))
    valid = l2 > 0
    if np.any(valid):
        ax.semilogy(step[valid], l2[valid], "-", color="#1f77b4", label=r"$L_2$ residual")
    if "residual_linf" in data and np.any(data["residual_linf"] > 0):
        ax.semilogy(
            step, data["residual_linf"], "-", color="#d62728", lw=1.2, label=r"$L_\infty$ residual"
        )
    if not np.any(valid):
        warnings.warn(f"[{case_name}] no positive residual values; log plot empty")
    style_axes(ax, f"{case_name}: residual history", "Step", "Residual norm (log)")
    ax.legend(frameon=True, fancybox=True)
    fig.tight_layout()
    return fig


def plot_forces(case_dir, case_name):
    data = read_csv(case_dir / "forces.csv")
    cols = require_cols(data, ["step", "cl", "cd"], case_name, "forces.csv")
    if cols is None:
        return None
    step, cl, cd = cols
    fig, ax = plt.subplots(figsize=(7, 4.5))
    ax.plot(step, cd, "-", color="#1f77b4", label=r"$C_D$")
    ax.plot(step, cl, "-", color="#d62728", label=r"$C_L$")
    style_axes(ax, f"{case_name}: force coefficients", "Step", "Force coefficient")
    ax.legend(frameon=True, fancybox=True)
    fig.tight_layout()
    return fig


def plot_surface_cp(case_dir, case_name):
    data = read_csv(case_dir / "surface.csv")
    cols = require_cols(data, ["x", "y", "cp"], case_name, "surface.csv")
    if cols is None:
        return None
    x, y, cp = cols
    fig, ax = plt.subplots(figsize=(7, 4.5))
    if "cylinder" in case_name.lower():
        # wall-pressure distribution vs circumferential angle (degrees)
        xc, yc = x.mean(), y.mean()
        angle = np.degrees(np.arctan2(y - yc, x - xc))
        ax.plot(angle, cp, "-o", ms=2.5, color="#1f77b4")
        ax.set_xlabel(r"Circumferential angle $\theta$ (deg)")
        ax.set_ylabel(r"$C_p$")
    else:
        ax.plot(x, cp, "-o", ms=2.5, color="#1f77b4", label=r"$C_p$")
        ax.set_xlabel(r"$x$")
        ax.set_ylabel(r"$C_p$")
        ax.legend(frameon=True, fancybox=True)
    style_axes(ax, f"{case_name}: surface pressure coefficient", ax.get_xlabel(), ax.get_ylabel())
    ax.set_title(f"{case_name}: surface pressure coefficient")
    fig.tight_layout()
    return fig


def plot_field(case_dir, case_name, varname, title_var, cmap):
    """Filled contour of a scalar field from field_final.vtk/.vtu."""
    field_path = None
    for name in ("field_final.vtk", "field_final.vtu"):
        cand = case_dir / name
        if cand.exists():
            field_path = cand
            break
    if field_path is None:
        # fallback: generate from surface data
        return plot_field_from_surface(case_dir, case_name, varname, title_var)

    field = read_field_file(field_path)
    if field is None:
        return None
    layout = build_field_triangulation(field)
    if layout is None:
        warnings.warn(
            f"[{case_name}] field file has no usable spatial layout; "
            f"falling back to surface data"
        )
        return plot_field_from_surface(case_dir, case_name, varname, title_var)

    values = field_values(field, layout, varname)
    if values is None:
        # Fallback: compute Mach from velocity vectors + pressure/density.
        if varname == "mach" and "velocity" in field.get("cell_data", {}):
            cell = field["cell_data"]
            if "pressure" in cell and "density" in cell:
                rho, p = cell["density"], cell["pressure"]
                with np.errstate(divide="ignore", invalid="ignore"):
                    a = np.sqrt(np.abs(GAMMA_DEFAULT * p / rho))
                    values = np.linalg.norm(cell["velocity"], axis=1) / a
                values[~np.isfinite(values)] = 0.0
            else:
                values = np.linalg.norm(cell["velocity"], axis=1)
        else:
            warnings.warn(f"[{case_name}] variable '{varname}' not found in field file")
            return None

    tri = layout["tri"]
    finite = np.isfinite(values)
    if np.count_nonzero(finite) < 4:
        warnings.warn(f"[{case_name}] variable '{varname}' has no finite values")
        return None

    # Clip percentile range so a few outliers don't collapse the colormap.
    lo, hi = np.percentile(values[finite], [1.0, 99.0])

    fig, ax = plt.subplots(figsize=(7.5, 5.5))
    cf = ax.tricontourf(tri, values, levels=48, cmap=cmap, vmin=lo, vmax=hi)
    ax.tricontour(tri, values, levels=12, colors="k", linewidths=0.3, alpha=0.4)
    ax.set_aspect("equal")
    cbar = fig.colorbar(cf, ax=ax, shrink=0.85, pad=0.02)
    cbar.set_label(title_var)
    style_axes(
        ax,
        f"{case_name}: {title_var} field (clipped 1-99 pct)",
        r"$x$",
        r"$y$",
    )
    fig.tight_layout()
    return fig


def plot_field_from_surface(case_dir, case_name, varname, title_var):
    """Fallback field figure from surface.csv when no VTK field exists."""
    data = read_csv(case_dir / "surface.csv")
    cols = require_cols(data, ["x", "y", varname], case_name, "surface.csv")
    if cols is None:
        return None
    x, y, vals = cols
    fig, ax = plt.subplots(figsize=(7, 4.5))
    sc = ax.scatter(x, y, c=vals, cmap="viridis" if varname == "mach" else "inferno", s=18)
    cbar = fig.colorbar(sc, ax=ax, shrink=0.85)
    cbar.set_label(title_var)
    style_axes(ax, f"{case_name}: {title_var} (surface data)", r"$x$", r"$y$")
    ax.set_aspect("equal", adjustable="box")
    fig.tight_layout()
    return fig


# ---------------------------------------------------------------------------
# Sanity checks
# ---------------------------------------------------------------------------
def _pressure_from_conserved(rho, rhou, rhov, rhoE, gamma):
    """Reconstruct pressure from conserved variables (finite-volume 2D)."""
    with np.errstate(divide="ignore", invalid="ignore"):
        ke = 0.5 * (rhou**2 + rhov**2) / rho
        p = (gamma - 1.0) * (rhoE - ke)
    return p


def csv_last_row_complete(path, expected_cols):
    """Return True if the file's last row has the full column count.

    A solver interrupted mid-write leaves a truncated final row; the last
    *complete* row is then the true final state.
    """
    try:
        with open(path, "rb") as f:
            f.seek(0, os.SEEK_END)
            if f.tell() == 0:
                return True
            f.seek(max(0, f.tell() - 512))
            tail = f.read().decode("utf-8", errors="replace")
        last_line = tail.strip().splitlines()[-1]
        return len(last_line.split(",")) == expected_cols
    except (OSError, IndexError):
        return True


def run_sanity_checks(case_dir, case_name, case_id):
    """Run the physics sanity gate from OUTPUT_CONTRACT.md for one case."""
    checks = {
        "case_id": case_id,
        "positive_density": None,
        "positive_pressure": None,
        "naca_symmetry": None,
    }

    def positivity(values):
        """True when every finite value is strictly positive."""
        finite = np.isfinite(values)
        if not np.any(finite):
            return None
        return bool(np.all(values[finite] > 0))

    res = read_csv(case_dir / "residuals.csv")
    if "rho" in res:
        rho = res["rho"]
        checks["positive_density"] = positivity(rho)
        checks["num_nonfinite_rows"] = int(np.count_nonzero(~np.isfinite(rho)))
        finite = np.isfinite(rho)
        checks["min_density"] = float(np.min(rho[finite])) if np.any(finite) else None
        if {"rhou", "rhov", "rhoE"}.issubset(res):
            p = _pressure_from_conserved(res["rho"], res["rhou"], res["rhov"], res["rhoE"], GAMMA_DEFAULT)
            checks["positive_pressure"] = positivity(p)
            finite_p = np.isfinite(p)
            checks["min_pressure"] = float(np.min(p[finite_p])) if np.any(finite_p) else None
    if "residual_l2" in res and len(res["residual_l2"]):
        checks["residuals_last_row_complete"] = csv_last_row_complete(
            case_dir / "residuals.csv", len(res)
        )

    # Prefer surface.csv pressure (directly reported).
    surf = {}
    if (case_dir / "surface.csv").exists():
        surf = read_csv(case_dir / "surface.csv")
    if "pressure" in surf:
        ps = surf["pressure"]
        checks["positive_pressure"] = positivity(ps)
        finite = np.isfinite(ps)
        checks["min_pressure"] = float(np.min(ps[finite])) if np.any(finite) else None
    if "cp" in surf:
        cp = surf["cp"]
        finite = np.isfinite(cp)
        checks["surface_cp_varies"] = bool(
            np.count_nonzero(finite) > 1 and np.ptp(cp[finite]) > 1e-12
        )

    # Forces: final coefficients + symmetry / drag / unsteadiness checks.
    # Use the last *complete* row for final values (truncated writes happen).
    forc = read_csv(case_dir / "forces.csv")
    if "cl" in forc and "cd" in forc:
        cl, cd = forc["cl"], forc["cd"]
        n = len(cl)
        complete = np.isfinite(cl) & np.isfinite(cd)
        last_ok = int(np.max(np.flatnonzero(complete))) if np.any(complete) else -1
        checks["forces_last_row_complete"] = csv_last_row_complete(
            case_dir / "forces.csv", len(forc)
        )
        tail = slice(max(0, n // 2), n) if n > 1 else slice(0, 1)
        tail_ok = complete[tail]
        checks["final_cl"] = float(cl[last_ok]) if last_ok >= 0 else None
        checks["final_cd"] = float(cd[last_ok]) if last_ok >= 0 else None
        checks["mean_tail_cl"] = float(np.mean(cl[tail][tail_ok])) if np.any(tail_ok) else None
        checks["mean_tail_cd"] = float(np.mean(cd[tail][tail_ok])) if np.any(tail_ok) else None
        if "cylinder" in case_name.lower():
            checks["positive_drag"] = bool(
                checks["mean_tail_cd"] is not None and checks["mean_tail_cd"] > 0
            )
            checks["unsteady_lift"] = bool(n > 10 and np.std(cl[tail][tail_ok]) > 1e-4)
        if "naca" in case_name.lower():
            # Robust 0-aoa symmetry check: tail-mean CL near zero.
            cl_mag = abs(checks["mean_tail_cl"]) if checks["mean_tail_cl"] is not None else float("inf")
            checks["naca_symmetry"] = bool(cl_mag < 1e-3)
            checks["naca_tail_cl_magnitude"] = cl_mag
            checks["naca_tail_cl_std"] = float(np.std(cl[tail][tail_ok])) if np.any(tail_ok) else None

    # Residual reduction (orders of magnitude).
    if "residual_l2" in res:
        l2 = res["residual_l2"]
        finite = np.isfinite(l2) & (l2 > 0)
        if np.count_nonzero(finite) > 1:
            r0, rf = l2[finite][0], l2[finite][-1]
            checks["residual_reduction_orders"] = float(np.log10(r0 / rf)) if rf > 0 else None

    # Field checks when VTK/VTU available.
    for name in ("field_final.vtk", "field_final.vtu"):
        field_path = case_dir / name
        if not field_path.exists():
            continue
        field = read_field_file(field_path)
        if field is None:
            continue
        dens = field.get("cell_data", {}).get("density")
        if dens is not None:
            checks["positive_density"] = positivity(dens)
            checks["min_density"] = float(np.min(dens[np.isfinite(dens)]))
        pres = field.get("cell_data", {}).get("pressure")
        if pres is not None:
            checks["positive_pressure"] = positivity(pres)
            checks["min_pressure"] = float(np.min(pres[np.isfinite(pres)]))
        break

    return checks


# ---------------------------------------------------------------------------
# Manifest
# ---------------------------------------------------------------------------
def manifest_row(fig_file, case_id, fig_type, variable, source_file, caption):
    return {
        "figure_file": fig_file,
        "case_id": case_id,
        "figure_type": fig_type,
        "variable": variable,
        "source_file": source_file,
        "caption": caption,
    }


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def discover_cases():
    """Yield (case_dir, case_name) for every non-empty result directory."""
    if not RESULTS_DIR.is_dir():
        warnings.warn(f"Results directory not found: {RESULTS_DIR}")
        return
    for entry in sorted(RESULTS_DIR.iterdir()):
        if entry.is_dir():
            has_output = any(entry.glob("*.csv")) or any(entry.glob("field_final.*"))
            if has_output:
                yield entry, entry.name


def case_id_for(case_dir, case_name):
    meta_path = case_dir / "metadata.json"
    try:
        with open(meta_path) as f:
            meta = json.load(f)
        cid = meta.get("case_id")
        if cid:
            return str(cid)
    except (OSError, json.JSONDecodeError):
        pass
    return case_name


def main():
    FIGURES_DIR.mkdir(parents=True, exist_ok=True)
    REPORT_DIR.mkdir(parents=True, exist_ok=True)

    manifest = []
    sanity = {
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "notes": (
            "positive_density/positive_pressure: all finite reported values strictly > 0. "
            "naca_symmetry: |tail-mean CL| < 1e-3 for 0-degree-aoa NACA cases. "
            "Cylinder checks: positive mean tail drag; unsteady lift std > 1e-4. "
            "*_last_row_complete: False flags a truncated final CSV row (interrupted write)."
        ),
        "checks": {},
    }

    cases = list(discover_cases())
    if not cases:
        warnings.warn(f"No case result directories found under {RESULTS_DIR}")
    for case_dir, case_name in cases:
        case_id = case_id_for(case_dir, case_name)
        print(f"\n=== {case_name} (case_id={case_id}) ===")

        generators = [
            ("residuals", plot_residuals, "residual_l2", "residuals.csv",
             f"Residual history (L2 norm, log scale) for {case_name}."),
            ("forces", plot_forces, "cl,cd", "forces.csv",
             f"Force coefficient history (CL, CD) for {case_name}."),
        ]
        if (case_dir / "surface.csv").exists():
            generators.append(
                ("surface_cp", plot_surface_cp, "cp", "surface.csv",
                 f"Surface pressure coefficient distribution for {case_name}.")
            )
        if any((case_dir / name).exists() for name in ("field_final.vtk", "field_final.vtu")):
            generators.append(
                ("mach", plot_field_generator("mach", "Mach number", "viridis"),
                 "mach", "field_final.vtk",
                 f"Mach number field for {case_name} (filled contours, clipped 1-99 pct).")
            )
            generators.append(
                ("pressure", plot_field_generator("pressure", "Pressure", "inferno"),
                 "pressure", "field_final.vtk",
                 f"Pressure field for {case_name} (filled contours, clipped 1-99 pct).")
            )
        elif (case_dir / "surface.csv").exists():
            # No field file: fall back to surface-based field figures.
            generators.append(
                ("mach", plot_field_generator("mach", "Mach number", "viridis"),
                 "mach", "surface.csv",
                 f"Mach number along body surface for {case_name} (surface-data fallback).")
            )
            generators.append(
                ("pressure", plot_field_generator("pressure", "Pressure", "inferno"),
                 "pressure", "surface.csv",
                 f"Pressure along body surface for {case_name} (surface-data fallback).")
            )

        for fig_type, func, variable, source_file, caption in generators:
            try:
                fig = func(case_dir, case_name)
            except Exception as exc:
                warnings.warn(f"[{case_name}] {fig_type} figure failed: {exc}")
                fig = None
            if fig is None:
                continue
            out_path = FIGURES_DIR / f"{case_name}_{fig_type}.png"
            fig.savefig(out_path, dpi=FIGURE_DPI)
            plt.close(fig)
            rel_source = os.path.relpath(case_dir / source_file, SOLVER_ROOT)
            manifest.append(
                manifest_row(
                    out_path.name, case_id, fig_type, variable,
                    rel_source, caption,
                )
            )
            print(f"  wrote {out_path.name}")

        sanity["checks"][case_name] = run_sanity_checks(case_dir, case_name, case_id)
        print(f"  sanity: {sanity['checks'][case_name]}")

    with open(MANIFEST_PATH, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(manifest_row("", "", "", "", "", "").keys()))
        writer.writeheader()
        writer.writerows(manifest)
    with open(SANITY_PATH, "w") as f:
        json.dump(sanity, f, indent=2)
        f.write("\n")

    print(f"\nWrote {len(manifest)} figures to {FIGURES_DIR}")
    print(f"Manifest:  {MANIFEST_PATH}")
    print(f"Sanity:    {SANITY_PATH}")


if __name__ == "__main__":
    main()
