#!/usr/bin/env python3
"""Plot residuals, forces, surface distributions, and fields for CFD solver results."""

import sys, os, csv, argparse
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from pathlib import Path

# Scholarly plot style
plt.rcParams.update({
    'font.size': 11, 'font.family': 'serif',
    'axes.labelsize': 12, 'axes.titlesize': 13,
    'legend.fontsize': 10, 'xtick.labelsize': 10, 'ytick.labelsize': 10,
    'figure.dpi': 150, 'savefig.dpi': 150,
    'savefig.bbox': 'tight', 'savefig.pad_inches': 0.05,
})


def read_csv(path):
    with open(path) as f:
        reader = csv.DictReader(f)
        return list(reader)


def plot_residuals(result_dir, out_dir, case_id):
    path = Path(result_dir) / 'residuals.csv'
    if not path.exists():
        print(f"  SKIP residuals: {path} not found")
        return None
    rows = read_csv(path)
    steps = [int(r['step']) for r in rows]
    res_l2 = [float(r['residual_l2']) for r in rows]

    fig, ax = plt.subplots(figsize=(6, 4))
    ax.semilogy(steps, res_l2, 'b-', linewidth=1.0)
    ax.set_xlabel('Step')
    ax.set_ylabel('Residual L2 norm')
    ax.set_title(f'{case_id}: Residual History')
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    out = Path(out_dir) / f'{case_id}_residuals.png'
    fig.savefig(out)
    plt.close(fig)
    print(f"  residuals: {out}")
    return out


def plot_forces(result_dir, out_dir, case_id):
    path = Path(result_dir) / 'forces.csv'
    if not path.exists():
        print(f"  SKIP forces: {path} not found")
        return None
    rows = read_csv(path)
    steps = [int(r['step']) for r in rows]
    cl = [float(r['cl']) for r in rows]
    cd = [float(r['cd']) for r in rows]

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(6, 5), sharex=True)
    ax1.plot(steps, cd, 'r-', linewidth=1.0, label='$C_D$')
    ax1.set_ylabel('$C_D$')
    ax1.grid(True, alpha=0.3)
    ax1.legend()

    ax2.plot(steps, cl, 'b-', linewidth=1.0, label='$C_L$')
    ax2.set_xlabel('Step')
    ax2.set_ylabel('$C_L$')
    ax2.grid(True, alpha=0.3)
    ax2.legend()

    fig.suptitle(f'{case_id}: Force History')
    fig.tight_layout()
    out = Path(out_dir) / f'{case_id}_forces.png'
    fig.savefig(out)
    plt.close(fig)
    print(f"  forces: {out}")
    return out


def plot_surface_cp(result_dir, out_dir, case_id):
    path = Path(result_dir) / 'surface.csv'
    if not path.exists():
        print(f"  SKIP surface: {path} not found")
        return None
    rows = read_csv(path)
    x = [float(r['x']) for r in rows]
    cp = [float(r['cp']) for r in rows]

    fig, ax = plt.subplots(figsize=(7, 4))
    ax.plot(x, cp, 'k-', linewidth=1.0)
    ax.set_xlabel('x')
    ax.set_ylabel('$C_p$')
    ax.set_title(f'{case_id}: Surface Pressure Coefficient')
    ax.invert_yaxis()
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    out = Path(out_dir) / f'{case_id}_cp.png'
    fig.savefig(out)
    plt.close(fig)
    print(f"  surface cp: {out}")
    return out


def plot_field_vtk(vtu_path, out_dir, case_id, field_name, cmap='viridis'):
    """Plot field from VTK file using simple ASCII VTK parser."""
    try:
        import xml.etree.ElementTree as ET
    except ImportError:
        print(f"  SKIP field: xml module not available")
        return None

    if not Path(vtu_path).exists():
        print(f"  SKIP field: {vtu_path} not found")
        return None

    tree = ET.parse(vtu_path)
    root = tree.getroot()

    # Find point coordinates
    ns = {'vtk': 'VTKFile'}
    piece = root.find('.//Piece')
    if piece is None:
        # Try without namespace
        piece = root.find('.//{*}Piece') or list(root.iter())[2]

    npoints = int(piece.get('NumberOfPoints'))
    ncells = int(piece.get('NumberOfCells'))

    # Read points
    pts_elem = piece.find('.//Points/DataArray')
    if pts_elem is None:
        pts_elem = piece.find('.//{*}Points/{*}DataArray')
    pts_text = ''.join(pts_elem.itertext()).strip()
    pts = np.fromstring(pts_text, sep=' ').reshape(npoints, 3)

    # Read cells (connectivity)
    cells_elem = piece.find('.//Cells/DataArray[@Name="connectivity"]')
    if cells_elem is None:
        for da in piece.iter():
            if da.tag.endswith('DataArray') and da.get('Name') == 'connectivity':
                cells_elem = da
                break
    if cells_elem is not None:
        conn_text = ''.join(cells_elem.itertext()).strip()
        conn = np.fromstring(conn_text, sep=' ').astype(int)
        offsets_text = ''.join(
            piece.find('.//Cells/DataArray[@Name="offsets"]').itertext()
        ).strip()
        offsets = np.fromstring(offsets_text, sep=' ').astype(int)
        types_text = ''.join(
            piece.find('.//Cells/DataArray[@Name="types"]').itertext()
        ).strip()
        cell_types = np.fromstring(types_text, sep=' ').astype(int)

    # Read cell data
    cell_data = {}
    for da in piece.findall('.//CellData/DataArray'):
        name = da.get('Name')
        text = ''.join(da.itertext()).strip()
        data = np.fromstring(text, sep=' ')
        if len(data) == ncells:
            cell_data[name] = data

    if field_name not in cell_data:
        available = list(cell_data.keys())
        # Try case-insensitive match
        for k in available:
            if k.lower() == field_name.lower():
                field_name = k
                break
        else:
            print(f"  SKIP field: '{field_name}' not in VTK. Available: {available}")
            return None

    field = cell_data[field_name]

    # Triangulate: for each cell, draw its vertices
    tri_verts = []
    tri_vals = []
    tri_indices = []
    offset_start = 0
    for ci in range(ncells):
        ct = cell_types[ci]
        off_end = offsets[ci]
        cell_conn = conn[offset_start:off_end]
        offset_start = off_end

        if ct == 5:  # TRI_3
            nv = len(tri_verts)
            for k in range(3):
                tri_verts.append(pts[cell_conn[k]])
                tri_vals.append(field[ci])
            tri_indices.append([nv, nv+1, nv+2])
        elif ct == 9:  # QUAD_4 -> split into 2 tris
            q = cell_conn
            nv = len(tri_verts)
            for k in [0, 1, 2]:
                tri_verts.append(pts[q[k]])
                tri_vals.append(field[ci])
            tri_indices.append([nv, nv+1, nv+2])
            nv = len(tri_verts)
            for k in [0, 2, 3]:
                tri_verts.append(pts[q[k]])
                tri_vals.append(field[ci])
            tri_indices.append([nv, nv+1, nv+2])

    tri_verts_np = np.array(tri_verts)
    tri_vals_np = np.array(tri_vals)
    tri_indices_np = np.array(tri_indices)

    fig, ax = plt.subplots(figsize=(8, 6))
    tpc = ax.tripcolor(tri_verts_np[:, 0], tri_verts_np[:, 1],
                       tri_indices_np, tri_vals_np,
                       shading='flat', cmap=cmap)
    cbar = fig.colorbar(tpc, ax=ax)
    cbar.set_label(field_name)

    ax.set_xlabel('x')
    ax.set_ylabel('y')
    ax.set_title(f'{case_id}: {field_name}')
    ax.set_aspect('equal')
    ax.grid(False)
    fig.tight_layout()
    out = Path(out_dir) / f'{case_id}_{field_name}.png'
    fig.savefig(out)
    plt.close(fig)
    print(f"  field {field_name}: {out}")
    return out


def main():
    parser = argparse.ArgumentParser(description='Plot CFD solver results')
    parser.add_argument('result_dir', help='Path to case result directory')
    parser.add_argument('--output', '-o', default='.', help='Output directory for figures')
    parser.add_argument('--case-id', default=None, help='Case ID (default: from result dir name)')
    parser.add_argument('--all', action='store_true', help='Generate all plot types')
    args = parser.parse_args()

    result_dir = Path(args.result_dir)
    out_dir = Path(args.output)
    out_dir.mkdir(parents=True, exist_ok=True)

    case_id = args.case_id or result_dir.name

    print(f"Plotting results for {case_id}")

    plot_residuals(result_dir, out_dir, case_id)
    plot_forces(result_dir, out_dir, case_id)
    plot_surface_cp(result_dir, out_dir, case_id)

    # Field plots
    vtu_path = result_dir / 'field_final.vtu'
    if vtu_path.exists():
        for field in ['density', 'pressure', 'mach']:
            plot_field_vtk(str(vtu_path), out_dir, case_id, field)


if __name__ == '__main__':
    main()
