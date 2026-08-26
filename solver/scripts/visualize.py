#!/usr/bin/env python3
"""Generate benchmark visualizations from solver output."""

import csv
import json
import os
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.tri as tri
import numpy as np

plt.rcParams.update({
    'font.size': 10,
    'axes.labelsize': 11,
    'axes.titlesize': 12,
    'legend.fontsize': 9,
    'xtick.labelsize': 9,
    'ytick.labelsize': 9,
    'figure.dpi': 200,
    'savefig.dpi': 200,
    'savefig.bbox': 'tight',
    'lines.linewidth': 1.5,
    'axes.grid': True,
    'grid.alpha': 0.3,
})


def read_vtu(path):
    tree = ET.parse(path)
    root = tree.getroot()
    piece = root.find('.//Piece')

    points_data = piece.find('.//Points/DataArray')
    coords = np.array([float(x) for x in points_data.text.split()])
    coords = coords.reshape(-1, 3)
    x, y = coords[:, 0], coords[:, 1]

    conn_data = piece.find('.//Cells/DataArray[@Name="connectivity"]')
    connectivity = [int(v) for v in conn_data.text.split()]
    offsets_data = piece.find('.//Cells/DataArray[@Name="offsets"]')
    offsets = [int(v) for v in offsets_data.text.split()]
    types_data = piece.find('.//Cells/DataArray[@Name="types"]')
    types = [int(v) for v in types_data.text.split()]

    triangles = []
    prev = 0
    for i, off in enumerate(offsets):
        nodes = connectivity[prev:off]
        if len(nodes) == 3:
            triangles.append(nodes)
        elif len(nodes) == 4:
            triangles.append([nodes[0], nodes[1], nodes[2]])
            triangles.append([nodes[0], nodes[2], nodes[3]])
        prev = off

    cell_data = {}
    for da in piece.findall('.//CellData/DataArray'):
        name = da.get('Name')
        ncomp = int(da.get('NumberOfComponents', '1'))
        vals = [float(v) for v in da.text.split()]
        if ncomp == 1:
            cell_data[name] = np.array(vals)
        else:
            cell_data[name] = np.array(vals).reshape(-1, ncomp)

    return x, y, np.array(triangles), cell_data, offsets


def cell_to_node_avg(x, y, triangles, cell_data_arr, offsets):
    """Interpolate cell-centered data to nodes by averaging."""
    ncells = len(offsets)
    nnodes = len(x)
    node_vals = np.zeros(nnodes)
    node_counts = np.zeros(nnodes)

    prev = 0
    ci = 0
    conn_flat = []
    for tri_row in triangles:
        for n in tri_row:
            conn_flat.append(n)

    # Expand cell data to match triangles (quads -> 2 triangles)
    tri_vals = []
    prev_off = 0
    for i, off in enumerate(offsets):
        npts = off - prev_off
        if npts == 3:
            tri_vals.append(cell_data_arr[i])
        elif npts == 4:
            tri_vals.append(cell_data_arr[i])
            tri_vals.append(cell_data_arr[i])
        prev_off = off
    tri_vals = np.array(tri_vals)

    for ti, tri_nodes in enumerate(triangles):
        for n in tri_nodes:
            node_vals[n] += tri_vals[ti]
            node_counts[n] += 1

    node_counts[node_counts == 0] = 1
    return node_vals / node_counts


def plot_contour(x, y, triangles, values, offsets, title, label, filename,
                 xlim=None, ylim=None, levels=50, cmap='jet', vmin=None, vmax=None):
    """Plot filled contour from cell-centered data."""
    node_vals = cell_to_node_avg(x, y, triangles, values, offsets)
    triang = tri.Triangulation(x, y, triangles)

    fig, ax = plt.subplots(figsize=(10, 5))
    kwargs = {'levels': levels, 'cmap': cmap}
    if vmin is not None:
        kwargs['vmin'] = vmin
    if vmax is not None:
        kwargs['vmax'] = vmax
    if vmin is not None and vmax is not None:
        kwargs['levels'] = np.linspace(vmin, vmax, levels)

    tcf = ax.tricontourf(triang, node_vals, **kwargs, extend='both')
    plt.colorbar(tcf, ax=ax, label=label, shrink=0.8)
    ax.set_aspect('equal')
    ax.set_xlabel('x')
    ax.set_ylabel('y')
    ax.set_title(title)
    if xlim:
        ax.set_xlim(xlim)
    if ylim:
        ax.set_ylim(ylim)
    fig.savefig(filename)
    plt.close(fig)


def read_csv_data(path):
    with open(path) as f:
        reader = csv.DictReader(f)
        rows = list(reader)
    return rows


def plot_residuals(case_dir, case_id, figdir):
    rows = read_csv_data(os.path.join(case_dir, 'residuals.csv'))
    steps = [int(float(r['step'])) for r in rows]
    l2 = [float(r['residual_l2']) for r in rows]
    rho = [float(r['rho']) for r in rows]

    fig, ax = plt.subplots(figsize=(8, 5))
    ax.semilogy(steps, l2, label='L2 total', color='C0')
    ax.semilogy(steps, rho, label=r'$\rho$', color='C1', alpha=0.7)
    ax.set_xlabel('Step' if 'transient' not in case_id else 'Physical time step')
    ax.set_ylabel('Residual')
    ax.set_title(f'Residual History — {case_id}')
    ax.legend()
    fig.savefig(os.path.join(figdir, f'{case_id}_residuals.png'))
    plt.close(fig)


def plot_forces(case_dir, case_id, figdir):
    rows = read_csv_data(os.path.join(case_dir, 'forces.csv'))
    steps = [int(float(r['step'])) for r in rows]
    cd = [float(r['cd']) for r in rows]
    cl = [float(r['cl']) for r in rows]

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(8, 7), sharex=True)
    ax1.plot(steps, cd, color='C0')
    ax1.set_ylabel('$C_D$')
    ax1.set_title(f'Force History — {case_id}')

    ax2.plot(steps, cl, color='C1')
    ax2.set_ylabel('$C_L$')
    ax2.set_xlabel('Step')
    fig.tight_layout()
    fig.savefig(os.path.join(figdir, f'{case_id}_forces.png'))
    plt.close(fig)


def plot_surface_cp(case_dir, case_id, figdir):
    rows = read_csv_data(os.path.join(case_dir, 'surface.csv'))
    x = [float(r['x']) for r in rows]
    cp = [float(r['cp']) for r in rows]

    fig, ax = plt.subplots(figsize=(8, 5))
    ax.plot(x, cp, '.', markersize=2, color='C0')
    ax.invert_yaxis()
    ax.set_xlabel('x/c')
    ax.set_ylabel('$C_p$')
    ax.set_title(f'Surface Pressure Coefficient — {case_id}')
    fig.savefig(os.path.join(figdir, f'{case_id}_cp.png'))
    plt.close(fig)


def plot_surface_cf(case_dir, case_id, figdir):
    rows = read_csv_data(os.path.join(case_dir, 'surface.csv'))
    x = [float(r['x']) for r in rows]
    cf = [float(r['cf']) for r in rows]
    if all(abs(c) < 1e-15 for c in cf):
        return
    fig, ax = plt.subplots(figsize=(8, 5))
    ax.plot(x, cf, '.', markersize=2, color='C2')
    ax.set_xlabel('x/c')
    ax.set_ylabel('$C_f$')
    ax.set_title(f'Skin Friction Coefficient — {case_id}')
    fig.savefig(os.path.join(figdir, f'{case_id}_cf.png'))
    plt.close(fig)


def get_zoom_limits(case_id):
    if 'naca' in case_id:
        return (-0.3, 1.5), (-0.5, 0.5)
    elif 'cylinder' in case_id:
        return (-2, 6), (-3, 3)
    return None, None


def visualize_case(case_dir, case_id, figdir):
    """Generate all visualizations for a single case."""
    os.makedirs(figdir, exist_ok=True)

    plot_residuals(case_dir, case_id, figdir)
    plot_forces(case_dir, case_id, figdir)
    plot_surface_cp(case_dir, case_id, figdir)
    plot_surface_cf(case_dir, case_id, figdir)

    vtu_path = os.path.join(case_dir, 'field_final.vtu')
    if not os.path.exists(vtu_path):
        print(f"  Warning: no field_final.vtu for {case_id}")
        return

    x, y, triangles, cell_data, offsets = read_vtu(vtu_path)
    xlim, ylim = get_zoom_limits(case_id)

    if 'Mach' in cell_data:
        mach = cell_data['Mach']
        vmax_mach = None
        if 'm200' in case_id:
            vmax_mach = 3.0
        elif 'm080' in case_id:
            vmax_mach = 1.2
        elif 'm015' in case_id or 'm010' in case_id:
            vmax_mach = 0.3

        plot_contour(x, y, triangles, mach, offsets,
                     f'Mach Number — {case_id}', 'Mach',
                     os.path.join(figdir, f'{case_id}_mach.png'),
                     xlim=xlim, ylim=ylim, vmin=0, vmax=vmax_mach)

    if 'Pressure' in cell_data:
        pressure = cell_data['Pressure']
        plot_contour(x, y, triangles, pressure, offsets,
                     f'Pressure — {case_id}', 'Pressure',
                     os.path.join(figdir, f'{case_id}_pressure.png'),
                     xlim=xlim, ylim=ylim)

    if 're200' in case_id and 'Velocity' in cell_data:
        vel = cell_data['Velocity']
        u, v = vel[:, 0], vel[:, 1]
        velmag = np.sqrt(u**2 + v**2)
        plot_contour(x, y, triangles, velmag, offsets,
                     f'Velocity Magnitude — {case_id}', '|V|',
                     os.path.join(figdir, f'{case_id}_velocity.png'),
                     xlim=(-2, 12), ylim=(-4, 4))


def generate_all(results_dir, report_dir):
    figdir = os.path.join(report_dir, 'figures')
    os.makedirs(figdir, exist_ok=True)

    cases = [d for d in os.listdir(results_dir)
             if os.path.isdir(os.path.join(results_dir, d))
             and os.path.exists(os.path.join(results_dir, d, 'metadata.json'))]

    manifest = []
    for case_id in sorted(cases):
        case_dir = os.path.join(results_dir, case_id)
        print(f"Visualizing {case_id}...")
        visualize_case(case_dir, case_id, figdir)

        for fig_file in sorted(os.listdir(figdir)):
            if fig_file.startswith(case_id) and fig_file.endswith('.png'):
                var = 'residual_l2'
                fig_type = 'line_plot'
                if '_mach' in fig_file:
                    var = 'mach'
                    fig_type = 'contour'
                elif '_pressure' in fig_file:
                    var = 'pressure'
                    fig_type = 'contour'
                elif '_velocity' in fig_file:
                    var = 'velocity_magnitude'
                    fig_type = 'contour'
                elif '_cp' in fig_file:
                    var = 'cp'
                elif '_cf' in fig_file:
                    var = 'cf'
                elif '_forces' in fig_file:
                    var = 'cd_cl'
                elif '_residuals' in fig_file:
                    var = 'residual_l2'

                entry = {
                    'figure_file': fig_file,
                    'case_id': case_id,
                    'figure_type': fig_type,
                    'variable': var,
                    'source_file': f'results/{case_id}/field_final.vtu' if fig_type == 'contour'
                                   else f'results/{case_id}/{"residuals" if "residual" in var else "forces" if "cd" in var else "surface"}.csv',
                    'caption': fig_file.replace('.png', '').replace('_', ' ').title()
                }
                if entry not in manifest:
                    manifest.append(entry)

    with open(os.path.join(report_dir, 'figure_manifest.csv'), 'w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=['figure_file', 'case_id', 'figure_type', 'variable', 'source_file', 'caption'])
        writer.writeheader()
        # Deduplicate
        seen = set()
        for entry in manifest:
            key = entry['figure_file']
            if key not in seen:
                seen.add(key)
                writer.writerow(entry)

    print(f"Generated {len(manifest)} figures")
    return manifest


if __name__ == '__main__':
    results_dir = sys.argv[1] if len(sys.argv) > 1 else '/workspace/solver/results'
    report_dir = sys.argv[2] if len(sys.argv) > 2 else '/workspace/solver/report'
    generate_all(results_dir, report_dir)
