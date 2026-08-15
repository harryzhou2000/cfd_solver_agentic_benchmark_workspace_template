#!/usr/bin/env python3
"""Generate all required plots, figures, and report artifacts for the CFD benchmark."""

import argparse
import csv
import json
import math
import os
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.tri as tri
import numpy as np

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
CASE_LABELS = {
    "naca0012_m015_inviscid": "NACA0012 M0.15 Inviscid",
    "naca0012_m080_inviscid": "NACA0012 M0.8 Inviscid",
    "naca0012_m200_inviscid": "NACA0012 M2.0 Inviscid",
    "naca0012_m015_laminar_re5000": "NACA0012 M0.15 Laminar Re5000",
    "naca0012_m080_laminar_re5000": "NACA0012 M0.8 Laminar Re5000",
    "naca0012_m200_laminar_re5000": "NACA0012 M2.0 Laminar Re5000",
    "cylinder_m010_laminar_re20": "Cylinder M0.1 Laminar Re20",
    "cylinder_m010_laminar_re200": "Cylinder M0.1 Laminar Re200",
}

PLOT_STYLE = {
    "figure.dpi": 150,
    "font.size": 10,
    "axes.labelsize": 11,
    "axes.titlesize": 12,
    "legend.fontsize": 9,
    "lines.linewidth": 1.2,
    "grid.alpha": 0.3,
    "savefig.bbox": "tight",
    "savefig.dpi": 150,
}


# ---------------------------------------------------------------------------
# VTK ASCII unstructured grid reader
# ---------------------------------------------------------------------------
def read_vtk_ascii(path: str):
    """Read an ASCII VTK unstructured grid file. Returns dict of arrays."""
    with open(path) as f:
        text = f.read()
    lines = text.splitlines()
    data = {}
    i = 0
    # Skip to POINTS
    while i < len(lines) and not lines[i].strip().startswith("POINTS"):
        i += 1
    if i < len(lines):
        parts = lines[i].strip().split()
        npts = int(parts[1])
        i += 1
        coords = []
        while len(coords) < npts and i < len(lines):
            line = lines[i].strip()
            if line:
                parts = line.split()
                if len(parts) >= 3:
                    coords.append([float(parts[0]), float(parts[1])])
            i += 1
        data["points"] = np.array(coords)

    # Skip to CELLS
    while i < len(lines) and not lines[i].strip().startswith("CELLS"):
        i += 1
    if i < len(lines):
        parts = lines[i].strip().split()
        ncells = int(parts[1])
        i += 1
        cells = []
        while len(cells) < ncells and i < len(lines):
            line = lines[i].strip()
            if line:
                parts = line.split()
                nv = int(parts[0])
                cells.append([int(p) for p in parts[1:1 + nv]])
            i += 1
        data["cells"] = cells

    # Cell types
    while i < len(lines) and not lines[i].strip().startswith("CELL_TYPES"):
        i += 1
    if i < len(lines):
        i += 1
        types = []
        while len(types) < ncells and i < len(lines):
            line = lines[i].strip()
            if line:
                types.append(int(line))
            i += 1
        data["cell_types"] = types

    # Cell data
    while i < len(lines) and not lines[i].strip().startswith("CELL_DATA"):
        i += 1
    if i < len(lines):
        nc = int(lines[i].strip().split()[1])
        i += 1
        scalars = {}
        vectors = {}
        while i < len(lines):
            line = lines[i].strip()
            if not line or line.startswith("POINTS") or line.startswith("CELLS"):
                break
            if line.startswith("SCALARS"):
                parts = line.split()
                sname = parts[1]
                i += 1
                while i < len(lines) and lines[i].strip().startswith("LOOKUP_TABLE"):
                    i += 1
                vals = []
                while len(vals) < nc and i < len(lines):
                    line = lines[i].strip()
                    if not line or line.startswith("SCALARS") or line.startswith("VECTORS") or line.startswith("LOOKUP_TABLE"):
                        i -= 1
                        break
                    for v in line.split():
                        vals.append(float(v))
                    i += 1
                scalars[sname] = np.array(vals[:nc])
            elif line.startswith("VECTORS"):
                parts = line.split()
                vname = parts[1]
                i += 1
                vx, vy, vz = [], [], []
                while len(vx) < nc and i < len(lines):
                    line = lines[i].strip()
                    if not line or line.startswith("SCALARS") or line.startswith("VECTORS"):
                        i -= 1
                        break
                    parts = line.split()
                    if len(parts) >= 3:
                        vx.append(float(parts[0]))
                        vy.append(float(parts[1]))
                        vz.append(float(parts[2]))
                    i += 1
                vectors[vname] = {"x": np.array(vx[:nc]), "y": np.array(vy[:nc]), "z": np.array(vz[:nc])}
            else:
                i += 1
        data["scalars"] = scalars
        data["vectors"] = vectors
    return data


def build_tri_scalars(data, scalar_name):
    """Build triangulation and map cell-centered scalar to per-triangle values."""
    points = data["points"]
    cells = data["cells"]
    types = data.get("cell_types", [])
    scalars = data.get("scalars", {})
    if scalar_name not in scalars:
        return None, None
    vals = scalars[scalar_name]
    tri_verts = []
    tri_vals = []
    for ci, (cell, ctype) in enumerate(zip(cells, types)):
        if ctype == 5:  # VTK_TRIANGLE
            tri_verts.append([cell[0], cell[1], cell[2]])
            tri_vals.append(vals[ci])
        elif ctype == 9:  # VTK_QUAD
            tri_verts.append([cell[0], cell[1], cell[2]])
            tri_verts.append([cell[0], cell[2], cell[3]])
            tri_vals.append(vals[ci])
            tri_vals.append(vals[ci])
    if not tri_verts:
        return None, None
    triang = tri.Triangulation(points[:, 0], points[:, 1], tri_verts)
    return triang, np.array(tri_vals)


def build_tri_vectors(data, vec_name):
    """Build triangulation and map cell-centered vector to per-triangle values."""
    points = data["points"]
    cells = data["cells"]
    types = data.get("cell_types", [])
    vectors = data.get("vectors", {})
    if vec_name not in vectors:
        return None, None, None
    vx = vectors[vec_name]["x"]
    vy = vectors[vec_name]["y"]
    tri_verts = []
    tri_vx, tri_vy = [], []
    for ci, (cell, ctype) in enumerate(zip(cells, types)):
        if ctype == 5:
            tri_verts.append([cell[0], cell[1], cell[2]])
            tri_vx.append(vx[ci])
            tri_vy.append(vy[ci])
        elif ctype == 9:
            tri_verts.append([cell[0], cell[1], cell[2]])
            tri_verts.append([cell[0], cell[2], cell[3]])
            tri_vx.append(vx[ci])
            tri_vy.append(vy[ci])
            tri_vx.append(vx[ci])
            tri_vy.append(vy[ci])
    if not tri_verts:
        return None, None, None
    triang = tri.Triangulation(points[:, 0], points[:, 1], tri_verts)
    return triang, np.array(tri_vx), np.array(tri_vy)


# ---------------------------------------------------------------------------
# Plotting helpers
# ---------------------------------------------------------------------------
def save_fig(fig, path: Path):
    fig.savefig(path)
    plt.close(fig)
    print(f"  wrote {path}")


def plot_residuals(case_id: str, res_csv: Path, fig_dir: Path):
    rows = []
    with open(res_csv, newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            rows.append(row)
    if not rows:
        return None
    steps = np.array([int(r["step"]) for r in rows])
    l2 = np.array([float(r["residual_l2"]) for r in rows])
    linf = np.array([float(r["residual_linf"]) for r in rows])

    fig, ax = plt.subplots(figsize=(8, 5))
    ax.semilogy(steps, l2, label=r"$L_2$ residual")
    ax.semilogy(steps, linf, label=r"$L_\infty$ residual", alpha=0.7)
    ax.set_xlabel("Step")
    ax.set_ylabel("Residual")
    ax.set_title(f"Residual History — {CASE_LABELS.get(case_id, case_id)}")
    ax.legend()
    ax.grid(True)
    path = fig_dir / f"{case_id}_residuals.png"
    save_fig(fig, path)
    return path


def plot_forces(case_id: str, force_csv: Path, fig_dir: Path):
    rows = []
    with open(force_csv, newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            rows.append(row)
    if not rows:
        return None
    steps = np.array([int(r["step"]) for r in rows])
    cd = np.array([float(r["cd"]) for r in rows])
    cl = np.array([float(r["cl"]) for r in rows])
    pd = np.array([float(r["pressure_drag"]) for r in rows])
    vd = np.array([float(r["viscous_drag"]) for r in rows])

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(8, 7), sharex=True)
    ax1.plot(steps, cd, label=r"$C_D$")
    ax1.plot(steps, pd, "--", label=r"$C_{D,p}$", alpha=0.7)
    ax1.plot(steps, vd, ":", label=r"$C_{D,v}$", alpha=0.7)
    ax1.set_ylabel("Drag coefficient")
    ax1.legend()
    ax1.grid(True)
    ax2.plot(steps, cl, label=r"$C_L$", color="C3")
    ax2.set_xlabel("Step")
    ax2.set_ylabel("Lift coefficient")
    ax2.legend()
    ax2.grid(True)
    fig.suptitle(f"Force History — {CASE_LABELS.get(case_id, case_id)}")
    path = fig_dir / f"{case_id}_forces.png"
    save_fig(fig, path)
    return path


def plot_forces_transient(case_id: str, force_csv: Path, fig_dir: Path):
    rows = []
    with open(force_csv, newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            rows.append(row)
    if not rows:
        return None
    time = np.array([float(r["physical_time"]) for r in rows])
    cd = np.array([float(r["cd"]) for r in rows])
    cl = np.array([float(r["cl"]) for r in rows])
    pd = np.array([float(r["pressure_drag"]) for r in rows])
    vd = np.array([float(r["viscous_drag"]) for r in rows])

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 8), sharex=True)
    ax1.plot(time, cd, label=r"$C_D$", linewidth=0.8)
    ax1.plot(time, pd, "--", label=r"$C_{D,p}$", alpha=0.6, linewidth=0.6)
    ax1.plot(time, vd, ":", label=r"$C_{D,v}$", alpha=0.6, linewidth=0.6)
    ax1.set_ylabel("Drag coefficient")
    ax1.legend()
    ax1.grid(True)
    ax2.plot(time, cl, label=r"$C_L$", color="C3", linewidth=0.8)
    ax2.set_xlabel("Physical time")
    ax2.set_ylabel("Lift coefficient")
    ax2.legend()
    ax2.grid(True)
    fig.suptitle(f"Force History (Transient) — {CASE_LABELS.get(case_id, case_id)}")
    path = fig_dir / f"{case_id}_forces.png"
    save_fig(fig, path)
    return path


def plot_surface_cp(case_id: str, surface_csv: Path, fig_dir: Path):
    rows = []
    with open(surface_csv, newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            rows.append(row)
    if not rows:
        return None
    x = np.array([float(r["x"]) for r in rows])
    cp = np.array([float(r["cp"]) for r in rows])
    cf = np.array([float(r["cf"]) for r in rows])
    mach = np.array([float(r["mach"]) for r in rows])

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 5))
    ax1.plot(x, cp, "o-", markersize=2, linewidth=0.8)
    ax1.set_xlabel("x")
    ax1.set_ylabel(r"$C_p$")
    ax1.set_title("Pressure Coefficient")
    ax1.grid(True)
    ax1.invert_yaxis()
    if np.any(np.abs(cf) > 1e-12):
        ax2.plot(x, cf, "o-", markersize=2, linewidth=0.8, color="C1")
        ax2.set_xlabel("x")
        ax2.set_ylabel(r"$C_f$")
        ax2.set_title("Skin-Friction Coefficient")
        ax2.grid(True)
    else:
        ax2.plot(x, mach, "o-", markersize=2, linewidth=0.8, color="C2")
        ax2.set_xlabel("x")
        ax2.set_ylabel("Mach")
        ax2.set_title("Surface Mach Number")
        ax2.grid(True)
    fig.suptitle(f"Surface Distribution — {CASE_LABELS.get(case_id, case_id)}")
    path = fig_dir / f"{case_id}_surface.png"
    save_fig(fig, path)
    return path


def plot_field_contour(case_id: str, field_vtk: Path, fig_dir: Path, variable: str,
                       zoom: bool = False, title_extra: str = ""):
    data = read_vtk_ascii(str(field_vtk))
    if "points" not in data:
        return None
    triang, tri_vals = build_tri_scalars(data, variable)
    if triang is None:
        return None
    pts = data["points"]
    is_naca = "naca" in case_id.lower()
    is_cylinder = "cylinder" in case_id.lower()
    vals = np.clip(tri_vals, np.percentile(tri_vals, 1), np.percentile(tri_vals, 99))

    fig, ax = plt.subplots(figsize=(10, 6))
    cnt = ax.tripcolor(triang, facecolors=vals, cmap="viridis")
    cbar = fig.colorbar(cnt, ax=ax, label=variable)

    if zoom and is_naca:
        ax.set_xlim(-0.1, 1.3)
        ax.set_ylim(-0.5, 0.5)
    elif zoom and is_cylinder:
        ax.set_xlim(-1, 3)
        ax.set_ylim(-1.5, 1.5)
    else:
        ax.set_aspect("equal")

    ax.set_xlabel("x")
    ax.set_ylabel("y")
    title = f"{variable} — {CASE_LABELS.get(case_id, case_id)}"
    if title_extra:
        title += f" ({title_extra})"
    ax.set_title(title)
    suffix = "_zoom" if zoom else ""
    path = fig_dir / f"{case_id}_{variable.lower()}{suffix}.png"
    save_fig(fig, path)
    return path


def plot_vorticity_wake(case_id: str, field_vtk: Path, fig_dir: Path):
    data = read_vtk_ascii(str(field_vtk))
    if "points" not in data:
        return None
    triang, tri_vx, tri_vy = build_tri_vectors(data, "Velocity")
    if triang is None:
        return None
    vel_mag = np.sqrt(tri_vx**2 + tri_vy**2)

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 5))
    cnt1 = ax1.tripcolor(triang, facecolors=vel_mag, cmap="viridis")
    fig.colorbar(cnt1, ax=ax1, label="Velocity magnitude")
    ax1.set_xlim(-1, 5)
    ax1.set_ylim(-2, 2)
    ax1.set_xlabel("x")
    ax1.set_ylabel("y")
    ax1.set_title(f"Velocity Magnitude (wake) — {CASE_LABELS.get(case_id, case_id)}")

    cnt2 = ax2.tripcolor(triang, facecolors=vel_mag, cmap="plasma")
    fig.colorbar(cnt2, ax=ax2, label="Velocity magnitude")
    ax2.set_xlim(0.5, 3)
    ax2.set_ylim(-1, 1)
    ax2.set_xlabel("x")
    ax2.set_ylabel("y")
    ax2.set_title("Wake Detail")
    path = fig_dir / f"{case_id}_velocity_wake.png"
    save_fig(fig, path)
    return path


# ---------------------------------------------------------------------------
# Process a single case
# ---------------------------------------------------------------------------
def process_case(case_id: str, results_dir: Path, fig_dir: Path):
    print(f"\n{'='*60}")
    print(f"Processing {case_id}")
    print(f"{'='*60}")
    figures = []

    res_csv = results_dir / "residuals.csv"
    if res_csv.exists():
        p = plot_residuals(case_id, res_csv, fig_dir)
        if p:
            figures.append({"figure_file": p.name, "case_id": case_id,
                            "figure_type": "line", "variable": "residual",
                            "source_file": "residuals.csv",
                            "caption": f"Residual history for {CASE_LABELS.get(case_id, case_id)}"})

    force_csv = results_dir / "forces.csv"
    if force_csv.exists():
        is_transient = "re200" in case_id.lower() and "cylinder" in case_id.lower()
        if is_transient:
            p = plot_forces_transient(case_id, force_csv, fig_dir)
        else:
            p = plot_forces(case_id, force_csv, fig_dir)
        if p:
            figures.append({"figure_file": p.name, "case_id": case_id,
                            "figure_type": "line", "variable": "force",
                            "source_file": "forces.csv",
                            "caption": f"Force history for {CASE_LABELS.get(case_id, case_id)}"})

    surface_csv = results_dir / "surface.csv"
    if surface_csv.exists():
        p = plot_surface_cp(case_id, surface_csv, fig_dir)
        if p:
            figures.append({"figure_file": p.name, "case_id": case_id,
                            "figure_type": "line", "variable": "surface",
                            "source_file": "surface.csv",
                            "caption": f"Surface distribution for {CASE_LABELS.get(case_id, case_id)}"})

    field_vtk = results_dir / "field_final.vtk"
    if field_vtk.exists():
        # Mach
        p = plot_field_contour(case_id, field_vtk, fig_dir, "Mach", zoom=False)
        if p:
            figures.append({"figure_file": p.name, "case_id": case_id,
                            "figure_type": "contour", "variable": "Mach",
                            "source_file": "field_final.vtk",
                            "caption": f"Mach number contours for {CASE_LABELS.get(case_id, case_id)}"})
        # Mach zoom
        p = plot_field_contour(case_id, field_vtk, fig_dir, "Mach", zoom=True, title_extra="near-body zoom")
        if p:
            figures.append({"figure_file": p.name, "case_id": case_id,
                            "figure_type": "contour", "variable": "Mach",
                            "source_file": "field_final.vtk",
                            "caption": f"Mach number contours (near-body) for {CASE_LABELS.get(case_id, case_id)}"})
        # Pressure
        p = plot_field_contour(case_id, field_vtk, fig_dir, "Pressure", zoom=False)
        if p:
            figures.append({"figure_file": p.name, "case_id": case_id,
                            "figure_type": "contour", "variable": "Pressure",
                            "source_file": "field_final.vtk",
                            "caption": f"Pressure contours for {CASE_LABELS.get(case_id, case_id)}"})
        # Pressure zoom
        p = plot_field_contour(case_id, field_vtk, fig_dir, "Pressure", zoom=True, title_extra="near-body zoom")
        if p:
            figures.append({"figure_file": p.name, "case_id": case_id,
                            "figure_type": "contour", "variable": "Pressure",
                            "source_file": "field_final.vtk",
                            "caption": f"Pressure contours (near-body) for {CASE_LABELS.get(case_id, case_id)}"})
        # Re200 wake
        if "re200" in case_id.lower() and "cylinder" in case_id.lower():
            p = plot_vorticity_wake(case_id, field_vtk, fig_dir)
            if p:
                figures.append({"figure_file": p.name, "case_id": case_id,
                                "figure_type": "contour", "variable": "velocity_wake",
                                "source_file": "field_final.vtk",
                                "caption": f"Wake velocity visualization for {CASE_LABELS.get(case_id, case_id)}"})
    return figures


# ---------------------------------------------------------------------------
# Sanity checks
# ---------------------------------------------------------------------------
def generate_sanity_checks(case_ids: list, results_base: Path):
    checks = []
    for case_id in case_ids:
        case_dir = results_base / case_id
        if not case_dir.exists():
            continue
        case_checks = {"case_id": case_id, "checks": []}

        field_vtk = case_dir / "field_final.vtk"
        if field_vtk.exists():
            data = read_vtk_ascii(str(field_vtk))
            if "scalars" in data:
                rho = data["scalars"].get("Density", np.array([1.0]))
                press = data["scalars"].get("Pressure", np.array([1.0]))
                case_checks["checks"].append({
                    "name": "positive_density", "pass": bool(np.all(rho > 0)),
                    "details": f"min rho = {rho.min():.6e}"})
                case_checks["checks"].append({
                    "name": "positive_pressure", "pass": bool(np.all(press > 0)),
                    "details": f"min p = {press.min():.6e}"})

        surface_csv = case_dir / "surface.csv"
        if surface_csv.exists():
            rows = []
            with open(surface_csv, newline="") as f:
                reader = csv.DictReader(f)
                for row in reader:
                    rows.append(row)
            if rows:
                u = np.array([float(r["u"]) for r in rows])
                v = np.array([float(r["v"]) for r in rows])
                cp = np.array([float(r["cp"]) for r in rows])
                vel_mag = np.sqrt(u**2 + v**2)
                # For inviscid slip walls, check normal velocity (should be ~0).
                # For viscous no-slip walls, check total velocity (should be ~0).
                is_inv = "inviscid" in case_id
                if is_inv:
                    nx = np.array([float(r["nx"]) for r in rows])
                    ny = np.array([float(r["ny"]) for r in rows])
                    vn = np.abs(u * nx + v * ny)
                    wall_vel = vn
                    thresh = 0.5
                else:
                    wall_vel = vel_mag
                    thresh = 0.5
                case_checks["checks"].append({
                    "name": "wall_velocity",
                    "pass": bool(wall_vel.max() < thresh),
                    "details": f"max wall vel = {wall_vel.max():.6e}"})
                cp_range = cp.max() - cp.min()
                case_checks["checks"].append({
                    "name": "cp_varies_along_body",
                    "pass": bool(cp_range > 1e-6),
                    "details": f"cp range = {cp_range:.6e}"})

        force_csv = case_dir / "forces.csv"
        if force_csv.exists():
            rows = []
            with open(force_csv, newline="") as f:
                reader = csv.DictReader(f)
                for row in reader:
                    rows.append(row)
            if rows:
                last = rows[-1]
                cd = float(last["cd"])
                cl = float(last["cl"])
                vd = float(last["viscous_drag"])
                vl = float(last["viscous_lift"])
                if "naca" in case_id:
                    case_checks["checks"].append({
                        "name": "naca_near_zero_lift", "pass": bool(abs(cl) < 1e-3),
                        "details": f"cl = {cl:.6e}"})
                if "inviscid" in case_id:
                    case_checks["checks"].append({
                        "name": "inviscid_zero_viscous_forces", "pass": bool(abs(vd) < 1e-8 and abs(vl) < 1e-8),
                        "details": f"vd = {vd:.6e}, vl = {vl:.6e}"})
                if "cylinder" in case_id:
                    case_checks["checks"].append({
                        "name": "cylinder_positive_drag", "pass": bool(cd > 0),
                        "details": f"cd = {cd:.6f}"})
                if "re200" in case_id and "cylinder" in case_id:
                    cl_values = np.array([float(r["cl"]) for r in rows])
                    cl_range = cl_values.max() - cl_values.min()
                    case_checks["checks"].append({
                        "name": "re200_unsteady_lift", "pass": bool(cl_range > 1e-4),
                        "details": f"cl range = {cl_range:.6e}"})
        checks.append(case_checks)
    return {"sanity_checks": checks}


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--results", type=str, default="/workspace/solver/results")
    parser.add_argument("--report", type=str, default="/workspace/solver/report")
    parser.add_argument("--cases", type=str, nargs="*")
    args = parser.parse_args()

    results_base = Path(args.results)
    report_dir = Path(args.report)
    fig_dir = report_dir / "figures"
    fig_dir.mkdir(parents=True, exist_ok=True)
    plt.style.use(PLOT_STYLE)

    all_case_ids = sorted(CASE_LABELS.keys())
    if args.cases:
        case_ids = [c for c in all_case_ids if c in args.cases]
    else:
        case_ids = [c for c in all_case_ids if (results_base / c).exists()]
    if not case_ids:
        print("No cases found!")
        sys.exit(1)
    print(f"Processing {len(case_ids)} cases: {case_ids}")

    all_figures = []
    for case_id in case_ids:
        case_dir = results_base / case_id
        if not case_dir.exists():
            continue
        figs = process_case(case_id, case_dir, fig_dir)
        all_figures.extend(figs)

    # Figure manifest
    with open(report_dir / "figure_manifest.csv", "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=["figure_file", "case_id", "figure_type",
                                                "variable", "source_file", "caption"])
        writer.writeheader()
        writer.writerows(all_figures)
    print(f"\nWrote figure_manifest.csv ({len(all_figures)} entries)")

    # Sanity checks
    with open(report_dir / "sanity_checks.json", "w") as f:
        json.dump(generate_sanity_checks(case_ids, results_base), f, indent=2)
    print("Wrote sanity_checks.json")

    # Run manifest
    with open(report_dir / "run_manifest.csv", "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["case_id", "mpi_ranks", "steps", "physical_time",
                         "residual_reduction", "wall_time", "status", "notes"])
        for case_id in case_ids:
            case_dir = results_base / case_id
            status_path = case_dir / "run_status.json"
            if status_path.exists():
                with open(status_path) as sf:
                    s = json.load(sf)
                writer.writerow([s.get("case_id", case_id), s.get("mpi_ranks", 1),
                                 s.get("final_step", 0), s.get("final_physical_time", 0.0),
                                 s.get("residual_reduction_orders", 0.0),
                                 s.get("wall_time_seconds", 0.0),
                                 s.get("convergence_status", "unknown"), s.get("notes", "")])
            else:
                writer.writerow([case_id, 1, 0, 0.0, 0.0, 0.0, "incomplete", ""])
    print("Wrote run_manifest.csv")
    print(f"\nDone! {len(all_figures)} figures generated.")


if __name__ == "__main__":
    main()
