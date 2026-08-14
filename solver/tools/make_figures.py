#!/usr/bin/env python3
"""Generate all required figures from solver output files."""
import argparse, csv, json, math, os, re, sys
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.tri import Triangulation

def read_csv(path):
    with open(path) as f:
        return list(csv.DictReader(f))

def read_field_vtu(path):
    """Read VTU ASCII file, return (pts, tri, cell_data, cell_types)."""
    with open(path) as f:
        content = f.read()
    def extract_text(pattern):
        m = re.search(pattern, content, re.DOTALL)
        if m: return m.group(1).strip().split()
        return None
    # Points
    pts_data = extract_text(r'<Points>.*?<DataArray[^>]*>([^<]+)</DataArray>.*?</Points>')
    if pts_data is None:
        raise ValueError(f"Cannot find Points data in {path}")
    pts = np.array([float(v) for v in pts_data]).reshape(-1, 3)
    # Connectivity, offsets, types
    conn = np.array([int(v) for v in extract_text(r'Name="connectivity"[^>]*>([^<]+)</DataArray>')])
    offsets = np.array([int(v) for v in extract_text(r'Name="offsets"[^>]*>([^<]+)</DataArray>')])
    types = np.array([int(v) for v in extract_text(r'Name="types"[^>]*>([^<]+)</DataArray>')])
    # Cell data
    field_map = {
        'Density': 'density', 'Pressure': 'pressure', 'MachNumber': 'mach',
        'Temperature': 'temperature', 'RankID': 'rank',
        'VelocityX': 'velocity_x', 'VelocityY': 'velocity_y',
    }
    data = {}
    for vtk_name, short_name in field_map.items():
        vals = extract_text(rf'Name="{vtk_name}"[^>]*>([^<]+)</DataArray>')
        if vals is not None:
            data[short_name] = np.array([float(v) for v in vals])
    if 'velocity_x' in data and 'velocity_y' in data:
        data['velocity'] = np.sqrt(data['velocity_x']**2 + data['velocity_y']**2)
    # Build triangulation + per-triangle cell values
    ncells = len(offsets)
    triangles = []
    tri_cell = []  # which cell each triangle belongs to
    start = 0
    cell_idx = 0
    for i in range(ncells):
        end = offsets[i]
        nodes = conn[start:end]
        start = end
        if len(nodes) == 3:
            triangles.append(nodes)
            tri_cell.append(cell_idx)
        elif len(nodes) == 4:
            triangles.append([nodes[0], nodes[1], nodes[2]])
            triangles.append([nodes[0], nodes[2], nodes[3]])
            tri_cell.append(cell_idx)
            tri_cell.append(cell_idx)
        cell_idx += 1
    triangles = np.array(triangles)
    tri = Triangulation(pts[:,0], pts[:,1], triangles)
    return pts, tri, data, tri_cell, types

def make_residual_plot(case_id, results_dir, out_dir):
    rows = read_csv(os.path.join(results_dir, 'residuals.csv'))
    steps = [int(r['step']) for r in rows]
    l2 = [float(r['residual_l2']) for r in rows]
    fig, ax = plt.subplots(figsize=(8, 5))
    ax.semilogy(steps, l2, 'b-', linewidth=1.5)
    ax.set_xlabel('Step')
    ax.set_ylabel('Residual L2')
    ax.set_title(f'{case_id} - Residual History')
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, f'{case_id}_residual.png'), dpi=150)
    plt.close(fig)

def make_force_plot(case_id, results_dir, out_dir):
    rows = read_csv(os.path.join(results_dir, 'forces.csv'))
    steps = [int(r['step']) for r in rows]
    cd = [float(r['cd']) for r in rows]
    cl = [float(r['cl']) for r in rows]
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(8, 7), sharex=True)
    ax1.plot(steps, cd, 'r-', linewidth=1.5, label='Cd')
    ax1.set_ylabel('Drag Coefficient')
    ax1.grid(True, alpha=0.3)
    ax1.legend()
    ax2.plot(steps, cl, 'b-', linewidth=1.5, label='Cl')
    ax2.set_xlabel('Step')
    ax2.set_ylabel('Lift Coefficient')
    ax2.grid(True, alpha=0.3)
    ax2.legend()
    fig.suptitle(f'{case_id} - Force History')
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, f'{case_id}_forces.png'), dpi=150)
    plt.close(fig)

def make_cp_plot(case_id, results_dir, out_dir, is_naca=True):
    rows = read_csv(os.path.join(results_dir, 'surface.csv'))
    xs = [float(r['x']) for r in rows]
    cp = [float(r['cp']) for r in rows]
    fig, ax = plt.subplots(figsize=(8, 5))
    if is_naca:
        ax.plot(xs, cp, 'bo', markersize=2, label='Cp')
        ax.invert_yaxis()
        ax.set_xlabel('x/c')
    else:
        ys = [float(r['y']) for r in rows]
        theta = [math.atan2(y, x) for x, y in zip(xs, ys)]
        ax.plot(theta, cp, 'bo', markersize=2, label='Cp')
        ax.set_xlabel('Angle (rad)')
    ax.set_ylabel('Cp')
    ax.set_title(f'{case_id} - Surface Pressure Coefficient')
    ax.grid(True, alpha=0.3)
    ax.legend()
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, f'{case_id}_cp.png'), dpi=150)
    plt.close(fig)

def make_field_contour(case_id, field_dir, out_dir, var, title):
    vtu_path = os.path.join(field_dir, 'field_final.vtu')
    if not os.path.exists(vtu_path):
        print(f"  WARNING: {vtu_path} not found, skipping")
        return
    pts, tri, data, tri_cell, types = read_field_vtu(vtu_path)
    var_map = {'mach': 'mach', 'pressure': 'pressure', 'density': 'density', 'velocity': 'velocity'}
    if var not in var_map:
        return
    key = var_map[var]
    if key not in data:
        print(f"  WARNING: {key} not in field data, available: {list(data.keys())}")
        return
    vals = data[key]
    if vals.ndim > 1:
        vals = vals[:, 0]
    # Map cell values to triangles
    tri_vals = vals[tri_cell]
    fig, ax = plt.subplots(figsize=(10, 6))
    tc = ax.tripcolor(tri, tri_vals, shading='flat', cmap='viridis')
    plt.colorbar(tc, ax=ax, label=var.capitalize())
    ax.set_aspect('equal')
    ax.set_title(f'{case_id} - {title}')
    fig.tight_layout()
    fname = f'{case_id}_{var}.png'
    fig.savefig(os.path.join(out_dir, fname), dpi=150)
    plt.close(fig)
    return fname

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('case_id')
    parser.add_argument('results_dir')
    parser.add_argument('--out_dir', default='figures')
    args = parser.parse_args()
    os.makedirs(args.out_dir, exist_ok=True)
    print(f"Generating figures for {args.case_id} from {args.results_dir}")
    make_residual_plot(args.case_id, args.results_dir, args.out_dir)
    make_force_plot(args.case_id, args.results_dir, args.out_dir)
    is_naca = 'naca' in args.case_id
    make_cp_plot(args.case_id, args.results_dir, args.out_dir, is_naca)
    make_field_contour(args.case_id, args.results_dir, args.out_dir, 'mach', 'Mach Number')
    make_field_contour(args.case_id, args.results_dir, args.out_dir, 'pressure', 'Pressure')
    if 're200' in args.case_id:
        make_field_contour(args.case_id, args.results_dir, args.out_dir, 'velocity', 'Velocity Magnitude')
    print(f"Done: {args.case_id}")

if __name__ == '__main__':
    main()
