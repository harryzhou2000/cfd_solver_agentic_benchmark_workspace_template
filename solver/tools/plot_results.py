#!/usr/bin/env python3
"""Generate CFD benchmark report figures from solver output.

Usage:
    .venv/bin/python3 tools/plot_results.py <results_dir> [--output-dir report/figures]

Reads residuals.csv, forces.csv, surface.csv, and field_final_*.vtu files
from the given results directory and writes publication-quality figures
into the output directory (default: report/figures/).

Figure naming convention:
    {case_id}_residual.png    - residual history (log scale)
    {case_id}_forces.png      - cl, cd history
    {case_id}_surface_cp.png  - surface pressure coefficient
    {case_id}_mach.png        - Mach number field contour
    {case_id}_pressure.png    - pressure field contour
    {case_id}_vorticity.png   - vorticity / wake (cylinder Re200 only)

The script handles missing files gracefully: if a data source is absent it
prints a warning and skips the corresponding figure rather than crashing.
"""
from __future__ import annotations
import argparse
import csv
import json
import sys
import xml.etree.ElementTree as ET
from pathlib import Path
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.tri as mtri

plt.rcParams.update({
    "font.size": 11,
    "font.family": "serif",
    "axes.linewidth": 0.8,
    "lines.linewidth": 1.5,
    "figure.dpi": 150,
    "savefig.dpi": 150,
    "savefig.bbox": "tight",
    "axes.grid": True,
    "grid.alpha": 0.4,
    "grid.linewidth": 0.5,
})

_LINE_COLORS = [
    "#1f77b4", "#ff7f0e", "#2ca02c", "#d62728",
    "#9467bd", "#8c564b", "#e377c2", "#7f7f7f",
]


def _parse_array(text, dtype=float):
    """Parse whitespace-separated text into a 1-D numpy array."""
    tokens = text.split()
    if not tokens:
        return np.array([], dtype=dtype)
    return np.array(tokens, dtype=dtype)


def parse_vtu(filepath):
    """Parse a single ASCII VTU (VTK UnstructuredGrid) XML file.

    Returns a dict with:
        points       - (N, 2) float array of node x, y coordinates
        triangles    - (M, 3) int array of point indices (cells fan-triangulated)
        tri_to_cell  - (M,)   int array: source cell index for each triangle
        cell_data    - dict: variable name -> (ncells,) or (ncells, ncomp) array
        num_cells    - int
    """
    tree = ET.parse(str(filepath))
    root = tree.getroot()
    piece = root.find(".//Piece")
    if piece is None:
        raise ValueError("No <Piece> element found in %s" % filepath)

    pts_da = piece.find(".//Points/DataArray")
    pts = _parse_array(pts_da.text, float)
    ncomp = int(pts_da.get("NumberOfComponents", 3))
    pts = pts.reshape(-1, ncomp)
    points = pts[:, :2]

    cells_elem = piece.find("Cells")
    conn = _parse_array(cells_elem.find('DataArray[@Name="connectivity"]').text, int)
    offsets = _parse_array(cells_elem.find('DataArray[@Name="offsets"]').text, int)
    types = _parse_array(cells_elem.find('DataArray[@Name="types"]').text, int)

    triangles = []
    tri_to_cell = []
    prev = 0
    for ci, off in enumerate(offsets):
        cell_conn = conn[prev:off]
        prev = off
        n = len(cell_conn)
        if n < 3:
            continue
        for j in range(1, n - 1):
            triangles.append([int(cell_conn[0]), int(cell_conn[j]), int(cell_conn[j + 1])])
            tri_to_cell.append(ci)

    triangles = np.array(triangles, dtype=int) if triangles else np.zeros((0, 3), dtype=int)
    tri_to_cell = np.array(tri_to_cell, dtype=int)

    cell_data = {}
    cd_elem = piece.find("CellData")
    if cd_elem is not None:
        for da in cd_elem.findall("DataArray"):
            name = da.get("Name", "")
            vals = _parse_array(da.text, float)
            cdim = int(da.get("NumberOfComponents", 1))
            if cdim > 1 and len(vals) % cdim == 0:
                vals = vals.reshape(-1, cdim)
            cell_data[name] = vals

    return {
        "points": points,
        "triangles": triangles,
        "tri_to_cell": tri_to_cell,
        "cell_data": cell_data,
        "num_cells": int(piece.get("NumberOfCells", len(tri_to_cell))),
    }


def load_field(results_dir):
    """Load and merge all VTU pieces from a results directory.

    Returns a dict with merged points, triangles, tri_to_cell, and cell_data,
    or None if no VTU files are found.
    """
    results_dir = Path(results_dir)
    vtu_files = sorted(results_dir.glob("field_final_*.vtu"))
    if not vtu_files:
        single = results_dir / "field_final.vtu"
        if single.exists():
            vtu_files = [single]
    if not vtu_files:
        return None

    pieces = [parse_vtu(f) for f in vtu_files]

    points = np.vstack([p["points"] for p in pieces])

    all_tris = []
    all_tri_cell = []
    pt_off = 0
    cell_off = 0
    for p in pieces:
        all_tris.append(p["triangles"] + pt_off)
        all_tri_cell.append(p["tri_to_cell"] + cell_off)
        pt_off += len(p["points"])
        cell_off += p["num_cells"]

    triangles = np.vstack(all_tris) if all_tris else np.zeros((0, 3), dtype=int)
    tri_to_cell = np.concatenate(all_tri_cell) if all_tri_cell else np.array([], dtype=int)

    # Filter degenerate triangles (duplicate vertices)
    if len(triangles) > 0:
        valid = (triangles[:, 0] != triangles[:, 1]) & \
                (triangles[:, 1] != triangles[:, 2]) & \
                (triangles[:, 0] != triangles[:, 2])
        triangles = triangles[valid]
        tri_to_cell = tri_to_cell[valid]

    cell_data = {}
    if pieces:
        for name in pieces[0]["cell_data"]:
            arrs = [p["cell_data"][name] for p in pieces if name in p["cell_data"]]
            if not arrs:
                continue
            if arrs[0].ndim == 1:
                cell_data[name] = np.concatenate(arrs)
            else:
                cell_data[name] = np.vstack(arrs)

    return {
        "points": points,
        "triangles": triangles,
        "tri_to_cell": tri_to_cell,
        "cell_data": cell_data,
    }


def cell_to_node(points, triangles, tri_to_cell, cell_values):
    """Average cell-centred values to mesh nodes.

    Uses unique (node, cell) pairs so that quad sub-triangles do not
    double-count contributions.
    """
    npts = len(points)
    node_vals = np.zeros(npts)
    count = np.zeros(npts)
    node_idx = triangles.ravel()
    cell_idx = np.repeat(tri_to_cell, 3)
    pairs = np.unique(np.column_stack([node_idx, cell_idx]), axis=0)
    np.add.at(node_vals, pairs[:, 0], cell_values[pairs[:, 1]])
    np.add.at(count, pairs[:, 0], 1.0)
    mask = count > 0
    node_vals[~mask] = np.nan
    node_vals[mask] /= count[mask]
    return node_vals


def compute_vorticity(field):
    """Compute vorticity (dv/dx - du/dy) at mesh nodes from the velocity field.
    Returns (vorticity, points, triangles) or None."""
    vel = field["cell_data"].get("Velocity")
    if vel is None or vel.ndim < 2 or vel.shape[1] < 2:
        return None
    points = field["points"]
    triangles = field["triangles"]
    tri_to_cell = field["tri_to_cell"]
    if len(triangles) == 0:
        return None
    # Deduplicate points by coordinate to avoid invalid triangulation
    rounded = np.round(points, decimals=8)
    _, unique_idx, inverse = np.unique(rounded, axis=0, return_index=True, return_inverse=True)
    points = points[unique_idx]
    triangles = inverse[triangles]
    # Re-filter degenerate triangles after dedup
    valid = (triangles[:, 0] != triangles[:, 1]) & \
            (triangles[:, 1] != triangles[:, 2]) & \
            (triangles[:, 0] != triangles[:, 2])
    triangles = triangles[valid]
    tri_to_cell = tri_to_cell[valid]
    u_node = cell_to_node(points, triangles, tri_to_cell, vel[:, 0])
    v_node = cell_to_node(points, triangles, tri_to_cell, vel[:, 1])
    tri = mtri.Triangulation(points[:, 0], points[:, 1], triangles)
    u_interp = mtri.LinearTriInterpolator(tri, np.nan_to_num(u_node))
    v_interp = mtri.LinearTriInterpolator(tri, np.nan_to_num(v_node))
    # In matplotlib 3.11+, gradient() requires (x, y) arrays
    px, py = points[:, 0], points[:, 1]
    du_dx, du_dy = u_interp.gradient(px, py)
    dv_dx, dv_dy = v_interp.gradient(px, py)
    vorticity = dv_dx - du_dy
    return vorticity, points, triangles


def _percentile_range(values, lo=1.0, hi=99.0):
    """Return (vmin, vmax) from percentile clipping of finite values."""
    finite = values[np.isfinite(values)]
    if len(finite) == 0:
        return 0.0, 1.0
    vmin = float(np.percentile(finite, lo))
    vmax = float(np.percentile(finite, hi))
    if vmax - vmin < 1e-14:
        vmax = vmin + 1e-14
    return vmin, vmax


def read_csv_file(filepath):
    """Read a CSV file into a list of dict rows. Returns None if missing."""
    filepath = Path(filepath)
    if not filepath.exists():
        return None
    with open(filepath, newline="") as f:
        return list(csv.DictReader(f))


def _col(rows, name, dtype=float):
    """Extract a numeric column from CSV rows."""
    return np.array([dtype(r[name]) for r in rows], dtype=dtype)


def get_case_info(results_dir):
    """Determine case_id and case type from metadata.json or directory name."""
    results_dir = Path(results_dir)
    case_id = results_dir.name
    metadata = {}
    meta_path = results_dir / "metadata.json"
    if meta_path.exists():
        try:
            metadata = json.loads(meta_path.read_text())
            case_id = metadata.get("case_id", case_id)
        except (json.JSONDecodeError, OSError):
            pass
    case_json_path = results_dir / "case_modified.json"
    case_json = {}
    if case_json_path.exists():
        try:
            case_json = json.loads(case_json_path.read_text())
        except (json.JSONDecodeError, OSError):
            pass
    cid_lower = case_id.lower()
    is_naca = "naca" in cid_lower
    is_cylinder = "cylinder" in cid_lower
    is_inviscid = "inviscid" in cid_lower
    is_re200 = "re200" in cid_lower
    is_transient = metadata.get("time_integrator", "") == "bdf2" or is_re200
    aoa = 0.0
    if case_json:
        aoa = case_json.get("freestream", {}).get("aoa_degrees", 0.0)
    return {
        "case_id": case_id,
        "is_naca": is_naca,
        "is_cylinder": is_cylinder,
        "is_inviscid": is_inviscid,
        "is_re200": is_re200,
        "is_transient": is_transient,
        "aoa": aoa,
        "metadata": metadata,
    }


def plot_residuals(rows, case_id, out_path):
    """Residual history plot (log scale)."""
    if not rows:
        print("  [skip] residuals.csv empty or missing for %s" % case_id)
        return False
    step = _col(rows, "step", int)
    res_vars = ["rho", "rhou", "rhov", "rhoE"]
    labels = [r"$\rho$", r"$\rho u$", r"$\rho v$", r"$\rho E$"]
    fig, ax = plt.subplots(figsize=(8, 5))
    for i, (var, label) in enumerate(zip(res_vars, labels)):
        vals = _col(rows, var)
        mask = vals > 0
        if mask.any():
            color = _LINE_COLORS[i % len(_LINE_COLORS)]
            ax.semilogy(step[mask], vals[mask], color=color, label=label, linewidth=1.2)
    for var, ls in [("residual_l2", "--"), ("residual_linf", ":")]:
        if var in rows[0]:
            vals = _col(rows, var)
            mask = vals > 0
            if mask.any():
                ax.semilogy(step[mask], vals[mask], color="k",
                            linestyle=ls, label=var, linewidth=1.0, alpha=0.7)
    ax.set_xlabel("Iteration step")
    ax.set_ylabel("Residual (log scale)")
    ax.set_title("Residual History - %s" % case_id)
    ax.legend(fontsize=9, loc="best")
    ax.grid(True, which="both", alpha=0.3)
    fig.tight_layout()
    fig.savefig(out_path)
    plt.close(fig)
    return True


def plot_forces(rows, case_id, out_path, is_transient=False):
    """Force coefficient history plot (cl, cd vs step or physical_time)."""
    if not rows:
        print("  [skip] forces.csv empty or missing for %s" % case_id)
        return False
    x_var = "physical_time" if is_transient else "step"
    x_label = "Physical time" if is_transient else "Iteration step"
    x = _col(rows, x_var, float)
    cl = _col(rows, "cl")
    cd = _col(rows, "cd")
    fig, ax1 = plt.subplots(figsize=(8, 5))
    color_cl = _LINE_COLORS[0]
    ax1.set_xlabel(x_label)
    ax1.set_ylabel(r"$C_L$", color=color_cl)
    ax1.plot(x, cl, color=color_cl, label=r"$C_L$", linewidth=1.2)
    ax1.tick_params(axis="y", labelcolor=color_cl)
    ax2 = ax1.twinx()
    color_cd = _LINE_COLORS[1]
    ax2.set_ylabel(r"$C_D$", color=color_cd)
    ax2.plot(x, cd, color=color_cd, label=r"$C_D$", linewidth=1.2)
    ax2.tick_params(axis="y", labelcolor=color_cd)
    lines1, labels1 = ax1.get_legend_handles_labels()
    lines2, labels2 = ax2.get_legend_handles_labels()
    ax1.legend(lines1 + lines2, labels1 + labels2, loc="best", fontsize=9)
    ax1.grid(True, alpha=0.3)
    ax1.set_title("Force Coefficient History - %s" % case_id)
    fig.tight_layout()
    fig.savefig(out_path)
    plt.close(fig)
    return True


def plot_surface_cp(rows, case_id, out_path, is_naca, is_cylinder):
    """Surface pressure coefficient plot.

    NACA: cp vs chord position x (separate upper/lower surfaces).
    Cylinder: cp vs angular position theta around the cylinder.
    """
    if not rows:
        print("  [skip] surface.csv empty or missing for %s" % case_id)
        return False
    x = _col(rows, "x")
    y = _col(rows, "y")
    cp = _col(rows, "cp")
    fig, ax = plt.subplots(figsize=(8, 5))
    if is_naca:
        upper = y >= 0
        lower = y < 0
        for mask, label, color in [
            (upper, "Upper", _LINE_COLORS[0]),
            (lower, "Lower", _LINE_COLORS[1]),
        ]:
            if mask.any():
                xs, cs = x[mask], cp[mask]
                order = np.argsort(xs)
                ax.plot(xs[order], cs[order], color=color, label=label,
                        marker=".", markersize=3, linewidth=1.0)
        ax.set_xlabel(r"$x / c$")
        ax.set_ylabel(r"$C_p$")
        ax.invert_yaxis()
    elif is_cylinder:
        theta = np.degrees(np.arctan2(y, x))
        order = np.argsort(theta)
        ax.plot(theta[order], cp[order], color=_LINE_COLORS[0],
                marker=".", markersize=3, linewidth=1.0)
        ax.set_xlabel(r"$\theta$ (degrees)")
        ax.set_ylabel(r"$C_p$")
        ax.invert_yaxis()
    else:
        ax.plot(x, cp, color=_LINE_COLORS[0], marker=".", markersize=3, linewidth=1.0)
        ax.set_xlabel("x")
        ax.set_ylabel(r"$C_p$")
    ax.set_title("Surface Pressure Coefficient - %s" % case_id)
    ax.legend(fontsize=9, loc="best")
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    fig.savefig(out_path)
    plt.close(fig)
    return True


def plot_field_contour(field, var_name, case_id, out_path,
                       is_naca, is_cylinder, is_re200, surface_rows=None):
    """Filled contour of a cell-centred field variable.

    Uses matplotlib triangulation with percentile-clipped colour range.
    """
    points = field["points"]
    triangles = field["triangles"]
    tri_to_cell = field["tri_to_cell"]
    cell_data = field["cell_data"]
    if var_name not in cell_data:
        print("  [skip] variable '%s' not found in VTU cell data" % var_name)
        return False
    if len(triangles) == 0:
        print("  [skip] no triangles for %s" % var_name)
        return False
    cell_vals = cell_data[var_name]
    if cell_vals.ndim > 1:
        cell_vals = cell_vals[:, 0]
    node_vals = cell_to_node(points, triangles, tri_to_cell, cell_vals)
    tri = mtri.Triangulation(points[:, 0], points[:, 1], triangles)
    vmin, vmax = _percentile_range(node_vals, 1.0, 99.0)
    fig, ax = plt.subplots(figsize=(8, 6))
    levels = np.linspace(vmin, vmax, 60)
    tcf = ax.tricontourf(tri, node_vals, levels=levels, cmap="jet", extend="both")
    cbar = fig.colorbar(tcf, ax=ax, shrink=0.9, pad=0.02)
    cbar.set_label(var_name)
    if surface_rows:
        sx = _col(surface_rows, "x")
        sy = _col(surface_rows, "y")
        ax.scatter(sx, sy, c="black", s=0.5, zorder=5)
    _set_view_window(ax, is_naca, is_cylinder, points)
    var_label = var_name.replace("_", " ")
    ax.set_title("%s Field - %s" % (var_label, case_id))
    ax.set_xlabel("x")
    ax.set_ylabel("y")
    ax.set_aspect("equal", adjustable="box")
    fig.tight_layout()
    fig.savefig(out_path)
    plt.close(fig)
    return True


def plot_vorticity(field, case_id, out_path, surface_rows=None):
    """Vorticity / wake visualization for cylinder Re200.

    Uses a clipped range of [-5, 5] as recommended by the benchmark spec.
    """
    result = compute_vorticity(field)
    if result is None:
        print("  [skip] could not compute vorticity (no velocity data)")
        return False
    vort, points, triangles = result
    if len(triangles) == 0:
        print("  [skip] no triangles for vorticity")
        return False
    tri = mtri.Triangulation(points[:, 0], points[:, 1], triangles)
    clip = 5.0
    vort_clipped = np.clip(vort, -clip, clip)
    fig, ax = plt.subplots(figsize=(10, 5))
    levels = np.linspace(-clip, clip, 60)
    try:
        tcf = ax.tricontourf(tri, vort_clipped, levels=levels, cmap="RdBu_r", extend="both")
    except RuntimeError:
        # Fallback: use tripcolor for invalid triangulations
        tcf = ax.tripcolor(tri, vort_clipped, shading="gouraud", cmap="RdBu_r",
                           vmin=-clip, vmax=clip)
    cbar = fig.colorbar(tcf, ax=ax, shrink=0.9, pad=0.02)
    cbar.set_label("Vorticity (clipped [-5, 5])")
    if surface_rows:
        sx = _col(surface_rows, "x")
        sy = _col(surface_rows, "y")
        ax.scatter(sx, sy, c="black", s=0.5, zorder=5)
    _set_view_window(ax, is_naca=False, is_cylinder=True, points=points)
    ax.set_title("Vorticity Field - %s" % case_id)
    ax.set_xlabel("x")
    ax.set_ylabel("y")
    ax.set_aspect("equal", adjustable="box")
    fig.tight_layout()
    fig.savefig(out_path)
    plt.close(fig)
    return True


def _set_view_window(ax, is_naca, is_cylinder, points):
    """Set a physically meaningful view window based on case type."""
    if is_naca:
        ax.set_xlim(-0.3, 1.3)
        ax.set_ylim(-0.4, 0.4)
    elif is_cylinder:
        ax.set_xlim(-2.0, 10.0)
        ax.set_ylim(-4.0, 4.0)
    else:
        margin_x = 0.05 * (points[:, 0].max() - points[:, 0].min())
        margin_y = 0.05 * (points[:, 1].max() - points[:, 1].min())
        ax.set_xlim(points[:, 0].min() - margin_x, points[:, 0].max() + margin_x)
        ax.set_ylim(points[:, 1].min() - margin_y, points[:, 1].max() + margin_y)


def main():
    parser = argparse.ArgumentParser(
        description="Generate CFD benchmark figures from solver output."
    )
    parser.add_argument("results_dir", type=str,
                        help="Path to a case results directory "
                             "(e.g. results/naca0012_m015_inviscid/)")
    parser.add_argument("--output-dir", type=str, default="report/figures",
                        help="Output directory for figures "
                             "(default: report/figures)")
    args = parser.parse_args()
    results_dir = Path(args.results_dir).resolve()
    output_dir = Path(args.output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    if not results_dir.exists():
        print("Error: results directory not found: %s" % results_dir)
        sys.exit(1)
    info = get_case_info(results_dir)
    case_id = info["case_id"]
    print("Generating figures for case: %s" % case_id)
    print("  results: %s" % results_dir)
    print("  output:  %s" % output_dir)
    generated = []
    res_rows = read_csv_file(results_dir / "residuals.csv")
    res_path = output_dir / ("%s_residual.png" % case_id)
    if plot_residuals(res_rows, case_id, res_path):
        generated.append(res_path.name)
        print("  [ok] %s" % res_path.name)
    force_rows = read_csv_file(results_dir / "forces.csv")
    force_path = output_dir / ("%s_forces.png" % case_id)
    if plot_forces(force_rows, case_id, force_path, info["is_transient"]):
        generated.append(force_path.name)
        print("  [ok] %s" % force_path.name)
    surf_rows = read_csv_file(results_dir / "surface.csv")
    surf_path = output_dir / ("%s_surface_cp.png" % case_id)
    if plot_surface_cp(surf_rows, case_id, surf_path,
                       info["is_naca"], info["is_cylinder"]):
        generated.append(surf_path.name)
        print("  [ok] %s" % surf_path.name)
    field = load_field(results_dir)
    if field is None:
        print("  [skip] no VTU field files found")
    else:
        mach_path = output_dir / ("%s_mach.png" % case_id)
        if plot_field_contour(field, "Mach", case_id, mach_path,
                              info["is_naca"], info["is_cylinder"],
                              info["is_re200"], surf_rows):
            generated.append(mach_path.name)
            print("  [ok] %s" % mach_path.name)
        pres_path = output_dir / ("%s_pressure.png" % case_id)
        if plot_field_contour(field, "Pressure", case_id, pres_path,
                              info["is_naca"], info["is_cylinder"],
                              info["is_re200"], surf_rows):
            generated.append(pres_path.name)
            print("  [ok] %s" % pres_path.name)
        if info["is_re200"]:
            vort_path = output_dir / ("%s_vorticity.png" % case_id)
            if plot_vorticity(field, case_id, vort_path, surf_rows):
                generated.append(vort_path.name)
                print("  [ok] %s" % vort_path.name)
    print("\nDone. %d figure(s) generated in %s" % (len(generated), output_dir))
    for name in generated:
        print("  - %s" % name)


if __name__ == "__main__":
    main()
