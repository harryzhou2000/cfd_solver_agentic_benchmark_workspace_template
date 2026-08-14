#!/usr/bin/env python3
"""Generate all required plots from CFD solver results."""
import sys
import os
import csv
import json
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.tri import Triangulation
import xml.etree.ElementTree as ET

RESULTS_DIR = sys.argv[1] if len(sys.argv) > 1 else "solver/results"
FIGURES_DIR = sys.argv[2] if len(sys.argv) > 2 else "solver/report/figures"
os.makedirs(FIGURES_DIR, exist_ok=True)

CASE_IDS = [
    "naca0012_m015_inviscid", "naca0012_m080_inviscid", "naca0012_m200_inviscid",
    "naca0012_m015_laminar_re5000", "naca0012_m080_laminar_re5000", "naca0012_m200_laminar_re5000",
    "cylinder_m010_laminar_re20", "cylinder_m010_laminar_re200"
]

def read_csv(path):
    rows = []
    with open(path) as f:
        reader = csv.DictReader(f)
        for r in reader:
            rows.append(r)
    return rows

def plot_residuals(case_id, outdir):
    path = os.path.join(outdir, "residuals.csv")
    if not os.path.exists(path): return
    rows = read_csv(path)
    steps = [int(r["step"]) for r in rows]
    l2 = [float(r["residual_l2"]) for r in rows]
    fig, ax = plt.subplots(figsize=(8, 5))
    ax.semilogy(steps, l2, 'b-', linewidth=1.0)
    ax.set_xlabel("Pseudo-time step")
    ax.set_ylabel("L2 residual")
    ax.set_title(f"Residual history: {case_id}")
    ax.grid(True, which='both', alpha=0.3)
    ax.legend(["L2 residual"])
    fig.tight_layout()
    fig.savefig(os.path.join(FIGURES_DIR, f"{case_id}_residual.png"), dpi=150)
    plt.close(fig)

def plot_forces(case_id, outdir):
    path = os.path.join(outdir, "forces.csv")
    if not os.path.exists(path): return
    rows = read_csv(path)
    steps = [int(r["step"]) for r in rows]
    cd = [float(r["cd"]) for r in rows]
    cl = [float(r["cl"]) for r in rows]
    fig, ax = plt.subplots(figsize=(8, 5))
    ax.plot(steps, cd, 'r-', linewidth=1.0, label="$C_D$")
    ax.plot(steps, cl, 'b-', linewidth=1.0, label="$C_L$")
    ax.set_xlabel("Step")
    ax.set_ylabel("Force coefficient")
    ax.set_title(f"Force history: {case_id}")
    ax.grid(True, alpha=0.3)
    ax.legend()
    fig.tight_layout()
    fig.savefig(os.path.join(FIGURES_DIR, f"{case_id}_forces.png"), dpi=150)
    plt.close(fig)

def plot_surface_cp(case_id, outdir):
    path = os.path.join(outdir, "surface.csv")
    if not os.path.exists(path): return
    rows = read_csv(path)
    x = [float(r["x"]) for r in rows]
    cp = [float(r["cp"]) for r in rows]
    fig, ax = plt.subplots(figsize=(8, 5))
    ax.scatter(x, cp, s=2, c='blue')
    ax.set_xlabel("x")
    ax.set_ylabel("$C_p$")
    ax.set_title(f"Surface pressure coefficient: {case_id}")
    ax.grid(True, alpha=0.3)
    ax.invert_yaxis()
    fig.tight_layout()
    fig.savefig(os.path.join(FIGURES_DIR, f"{case_id}_cp.png"), dpi=150)
    plt.close(fig)

def parse_vtu(path):
    tree = ET.parse(path)
    root = tree.getroot()
    piece = root.find(".//Piece")
    if piece is None: return None
    npts = int(piece.get("NumberOfPoints"))
    ncells = int(piece.get("NumberOfCells"))
    pts_arr = None
    for da in root.findall(".//Points/DataArray"):
        vals = list(map(float, da.text.split()))
        pts_arr = np.array(vals).reshape(-1, 3)
        break
    cell_data = {}
    for da in root.findall(".//CellData/DataArray"):
        name = da.get("Name")
        vals = list(map(float, da.text.split()))
        cell_data[name] = np.array(vals)
    conn = []
    offsets = []
    types = []
    for da in root.findall(".//Cells/DataArray"):
        if da.get("Name") == "connectivity":
            conn = list(map(int, da.text.split()))
        elif da.get("Name") == "offsets":
            offsets = list(map(int, da.text.split()))
        elif da.get("Name") == "types":
            types = list(map(int, da.text.split()))
    tris = []
    off = 0
    for i, t in enumerate(types):
        if t == 5:
            tris.append([conn[off], conn[off+1], conn[off+2]])
        elif t == 9:
            tris.append([conn[off], conn[off+1], conn[off+2]])
            tris.append([conn[off], conn[off+2], conn[off+3]])
        off = offsets[i]
    if not tris: return None
    tris_arr = np.array(tris)
    cx = np.zeros(ncells)
    cy = np.zeros(ncells)
    off = 0
    for i in range(ncells):
        nv = offsets[i] - (offsets[i-1] if i > 0 else 0)
        idx = conn[off:off+nv]
        cx[i] = np.mean(pts_arr[idx, 0])
        cy[i] = np.mean(pts_arr[idx, 1])
        off = offsets[i]
    return pts_arr[:, :2], tris_arr, cx, cy, cell_data

def plot_field(case_id, outdir, varname, label, clip_range=None):
    path = os.path.join(outdir, "field_final.vtu")
    if not os.path.exists(path): return
    result = parse_vtu(path)
    if result is None: return
    pts, tris, cx, cy, cell_data = result
    if varname not in cell_data: return
    vals = cell_data[varname]
    fig, ax = plt.subplots(figsize=(10, 7))
    # Use percentile-based color range to avoid outlier collapse
    vmin, vmax = np.percentile(vals, 2), np.percentile(vals, 98)
    if clip_range:
        vmin, vmax = clip_range
    elif vmax - vmin < 1e-15:
        vmin, vmax = vals.min(), vals.max()
    try:
        tcf = ax.tricontourf(cx, cy, vals, levels=50, vmin=vmin, vmax=vmax)
        plt.colorbar(tcf, ax=ax, label=label)
    except Exception:
        ax.scatter(cx, cy, c=vals, s=0.5, vmin=vmin, vmax=vmax)
        plt.colorbar(ax.collections[0], ax=ax, label=label)
    ax.set_aspect('equal')
    ax.set_xlabel("x")
    ax.set_ylabel("y")
    ax.set_title(f"{label}: {case_id}")
    fig.tight_layout()
    fname = varname.lower().replace(" ", "_")
    fig.savefig(os.path.join(FIGURES_DIR, f"{case_id}_{fname}.png"), dpi=150)
    plt.close(fig)

def main():
    for case_id in CASE_IDS:
        outdir = os.path.join(RESULTS_DIR, case_id)
        if not os.path.exists(outdir):
            print(f"Skipping {case_id} (no results)")
            continue
        print(f"Plotting {case_id}...")
        plot_residuals(case_id, outdir)
        plot_forces(case_id, outdir)
        plot_surface_cp(case_id, outdir)
        plot_field(case_id, outdir, "mach", "Mach number")
        plot_field(case_id, outdir, "pressure", "Pressure")
        if "re200" in case_id:
            # Velocity magnitude wake visualization
            plot_field(case_id, outdir, "velocity_u", "velocity")
            # Also generate a vorticity-like field using velocity components
            plot_field(case_id, outdir, "mach", "velocity_magnitude")
    print("All plots generated.")

if __name__ == "__main__":
    main()
