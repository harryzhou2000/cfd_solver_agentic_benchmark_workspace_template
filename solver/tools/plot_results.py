#!/usr/bin/env python3
"""Generate all report figures from solver output."""
import os
import sys
import json
import csv
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.tri as mtri
from pathlib import Path

plt.rcParams.update({
    'font.size': 10,
    'axes.labelsize': 11,
    'axes.titlesize': 12,
    'legend.fontsize': 9,
    'xtick.labelsize': 9,
    'ytick.labelsize': 9,
    'figure.dpi': 150,
    'savefig.dpi': 150,
    'savefig.bbox': 'tight',
    'lines.linewidth': 1.5,
    'grid.alpha': 0.3,
})

SOLVER_DIR = Path(__file__).parent.parent
RESULTS_DIR = SOLVER_DIR / 'results'
FIGURES_DIR = SOLVER_DIR / 'report' / 'figures'


def read_csv(filepath):
    """Read CSV file into dict of numpy arrays."""
    data = {}
    with open(filepath, 'r') as f:
        reader = csv.DictReader(f)
        rows = list(reader)
    if not rows:
        return data
    for key in rows[0]:
        try:
            data[key] = np.array([float(r[key]) for r in rows])
        except (ValueError, KeyError):
            data[key] = np.array([r[key] for r in rows])
    return data


def read_vtk_field(filepath):
    """Read VTK unstructured grid (ASCII) and return cell data."""
    points = []
    cells = []
    cell_types = []
    cell_data = {}
    current_scalar = None

    with open(filepath, 'r') as f:
        lines = f.readlines()

    i = 0
    n_points = 0
    n_cells = 0
    reading_points = False
    reading_cells = False
    reading_cell_types = False
    reading_cell_data = False
    reading_scalar_values = False
    scalar_name = None

    while i < len(lines):
        line = lines[i].strip()

        if line.startswith('POINTS'):
            n_points = int(line.split()[1])
            reading_points = True
            i += 1
            continue

        if reading_points and len(points) < n_points:
            parts = line.split()
            if len(parts) >= 3:
                points.append([float(parts[0]), float(parts[1])])
            if len(points) >= n_points:
                reading_points = False
            i += 1
            continue

        if line.startswith('CELLS'):
            parts = line.split()
            n_cells = int(parts[1])
            reading_cells = True
            i += 1
            continue

        if reading_cells and len(cells) < n_cells:
            parts = [int(x) for x in line.split()]
            nn = parts[0]
            cells.append(parts[1:1+nn])
            if len(cells) >= n_cells:
                reading_cells = False
            i += 1
            continue

        if line.startswith('CELL_TYPES'):
            reading_cell_types = True
            i += 1
            continue

        if reading_cell_types:
            cell_types.append(int(line))
            if len(cell_types) >= n_cells:
                reading_cell_types = False
            i += 1
            continue

        if line.startswith('CELL_DATA'):
            reading_cell_data = True
            i += 1
            continue

        if reading_cell_data and line.startswith('SCALARS'):
            scalar_name = line.split()[1]
            cell_data[scalar_name] = []
            i += 1  # skip LOOKUP_TABLE
            if i < len(lines) and lines[i].strip().startswith('LOOKUP_TABLE'):
                i += 1
            reading_scalar_values = True
            continue

        if reading_scalar_values:
            cell_data[scalar_name].append(float(line))
            if len(cell_data[scalar_name]) >= n_cells:
                reading_scalar_values = False
                cell_data[scalar_name] = np.array(cell_data[scalar_name])
            i += 1
            continue

        i += 1

    points = np.array(points)

    # Compute cell centroids
    centroids = np.zeros((n_cells, 2))
    for ci, cell_nodes in enumerate(cells):
        cx, cy = 0, 0
        for ni in cell_nodes:
            cx += points[ni][0]
            cy += points[ni][1]
        centroids[ci] = [cx / len(cell_nodes), cy / len(cell_nodes)]

    return points, cells, centroids, cell_data


def plot_residual_history(case_id, outdir):
    """Plot residual convergence history."""
    data = read_csv(outdir / 'residuals.csv')
    if 'step' not in data or len(data['step']) == 0:
        return

    fig, ax = plt.subplots(figsize=(7, 4.5))
    steps = data['step']
    res = data['residual_l2']

    mask = np.isfinite(res) & (res > 0)
    if mask.sum() == 0:
        return

    ax.semilogy(steps[mask], res[mask], 'b-', label='$||R||_2$')
    ax.set_xlabel('Iteration')
    ax.set_ylabel('Residual $L_2$ Norm')
    ax.set_title(f'Residual History: {case_id}')
    ax.grid(True, alpha=0.3)
    ax.legend()

    fig.savefig(FIGURES_DIR / f'{case_id}_residual.png')
    plt.close(fig)


def plot_force_history(case_id, outdir):
    """Plot force coefficient history."""
    data = read_csv(outdir / 'forces.csv')
    if 'step' not in data or len(data['step']) == 0:
        return

    steps = data['step']
    time_key = 'physical_time'
    x_data = data.get(time_key, steps)
    x_label = 'Physical Time' if time_key in data and np.any(data[time_key] > 0) else 'Iteration'
    if x_label == 'Iteration':
        x_data = steps

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(7, 7), sharex=True)

    mask = np.isfinite(data['cd']) & np.isfinite(data['cl'])
    if mask.sum() == 0:
        return

    ax1.plot(x_data[mask], data['cd'][mask], 'r-', label='$C_D$')
    ax1.set_ylabel('$C_D$')
    ax1.set_title(f'Force History: {case_id}')
    ax1.grid(True, alpha=0.3)
    ax1.legend()

    ax2.plot(x_data[mask], data['cl'][mask], 'b-', label='$C_L$')
    ax2.set_xlabel(x_label)
    ax2.set_ylabel('$C_L$')
    ax2.grid(True, alpha=0.3)
    ax2.legend()

    fig.tight_layout()
    fig.savefig(FIGURES_DIR / f'{case_id}_forces.png')
    plt.close(fig)


def plot_surface_cp(case_id, outdir):
    """Plot surface pressure coefficient."""
    data = read_csv(outdir / 'surface.csv')
    if 'x' not in data or len(data['x']) == 0:
        return

    fig, ax = plt.subplots(figsize=(7, 4.5))
    x = data['x']
    cp = data['cp']

    mask = np.isfinite(cp)
    ax.plot(x[mask], cp[mask], 'b.', markersize=2, label='$C_p$')
    ax.set_xlabel('$x/c$')
    ax.set_ylabel('$C_p$')
    ax.invert_yaxis()
    ax.set_title(f'Surface Pressure Coefficient: {case_id}')
    ax.grid(True, alpha=0.3)
    ax.legend()

    fig.savefig(FIGURES_DIR / f'{case_id}_cp.png')
    plt.close(fig)


def plot_surface_cf(case_id, outdir):
    """Plot surface skin friction coefficient for viscous cases."""
    data = read_csv(outdir / 'surface.csv')
    if 'x' not in data or 'cf' not in data or len(data['x']) == 0:
        return

    cf = data['cf']
    if np.all(np.abs(cf) < 1e-15):
        return

    fig, ax = plt.subplots(figsize=(7, 4.5))
    x = data['x']
    mask = np.isfinite(cf)
    ax.plot(x[mask], cf[mask], 'r.', markersize=2, label='$C_f$')
    ax.set_xlabel('$x/c$')
    ax.set_ylabel('$C_f$')
    ax.set_title(f'Surface Skin Friction: {case_id}')
    ax.grid(True, alpha=0.3)
    ax.legend()

    fig.savefig(FIGURES_DIR / f'{case_id}_cf.png')
    plt.close(fig)


def plot_field_contour(case_id, outdir, variable, vmin=None, vmax=None, cmap='jet', zoom=None):
    """Plot field contour from VTK file."""
    vtk_file = outdir / 'field_final.vtk'
    if not vtk_file.exists():
        return

    points, cells, centroids, cell_data = read_vtk_field(vtk_file)
    if variable not in cell_data:
        print(f"  WARNING: {variable} not found in {vtk_file}")
        return

    values = cell_data[variable]
    mask = np.isfinite(values)
    if mask.sum() == 0:
        return

    # Build triangulation from cell connectivity
    triangles = []
    tri_values = []
    for ci, cell_nodes in enumerate(cells):
        if not mask[ci]:
            continue
        if len(cell_nodes) == 3:
            triangles.append(cell_nodes)
            tri_values.append(values[ci])
        elif len(cell_nodes) == 4:
            triangles.append([cell_nodes[0], cell_nodes[1], cell_nodes[2]])
            tri_values.append(values[ci])
            triangles.append([cell_nodes[0], cell_nodes[2], cell_nodes[3]])
            tri_values.append(values[ci])

    if not triangles:
        return

    triangles = np.array(triangles)
    tri_values = np.array(tri_values)
    triang = mtri.Triangulation(points[:, 0], points[:, 1], triangles)

    if vmin is None:
        vmin = np.percentile(tri_values[np.isfinite(tri_values)], 1)
    if vmax is None:
        vmax = np.percentile(tri_values[np.isfinite(tri_values)], 99)

    fig, ax = plt.subplots(figsize=(10, 6))
    tpc = ax.tripcolor(triang, tri_values, cmap=cmap, vmin=vmin, vmax=vmax, shading='flat')
    cbar = fig.colorbar(tpc, ax=ax, shrink=0.8)
    cbar.set_label(variable)
    ax.set_xlabel('x')
    ax.set_ylabel('y')
    ax.set_aspect('equal')
    ax.set_title(f'{variable}: {case_id}')

    if zoom:
        ax.set_xlim(zoom[0], zoom[1])
        ax.set_ylim(zoom[2], zoom[3])

    varname = variable.lower().replace(' ', '_')
    suffix = '_zoom' if zoom else ''
    fig.savefig(FIGURES_DIR / f'{case_id}_{varname}{suffix}.png')
    plt.close(fig)


def process_case(case_id):
    """Generate all figures for a single case."""
    outdir = RESULTS_DIR / case_id
    if not outdir.exists():
        print(f"Skipping {case_id}: no results directory")
        return

    print(f"Processing {case_id}...")

    plot_residual_history(case_id, outdir)
    plot_force_history(case_id, outdir)
    plot_surface_cp(case_id, outdir)
    plot_surface_cf(case_id, outdir)

    is_naca = 'naca' in case_id
    is_cylinder = 'cylinder' in case_id

    if is_naca:
        plot_field_contour(case_id, outdir, 'Mach', cmap='jet')
        plot_field_contour(case_id, outdir, 'Pressure', cmap='jet')
        plot_field_contour(case_id, outdir, 'Mach', cmap='jet', zoom=(-0.5, 1.5, -0.5, 0.5))
        plot_field_contour(case_id, outdir, 'Pressure', cmap='jet', zoom=(-0.5, 1.5, -0.5, 0.5))
    elif is_cylinder:
        plot_field_contour(case_id, outdir, 'Mach', cmap='jet')
        plot_field_contour(case_id, outdir, 'Pressure', cmap='jet')

        if 're200' in case_id:
            plot_field_contour(case_id, outdir, 'VelocityX', cmap='RdBu_r',
                             zoom=(-2, 15, -5, 5))
            plot_field_contour(case_id, outdir, 'Mach', cmap='jet',
                             zoom=(-2, 15, -5, 5))
            plot_field_contour(case_id, outdir, 'Pressure', cmap='jet',
                             zoom=(-2, 15, -5, 5))
        else:
            plot_field_contour(case_id, outdir, 'Mach', cmap='jet',
                             zoom=(-2, 8, -3, 3))
            plot_field_contour(case_id, outdir, 'Pressure', cmap='jet',
                             zoom=(-2, 8, -3, 3))


def generate_figure_manifest():
    """Generate CSV manifest mapping figures to source data."""
    manifest = []
    for fig_path in sorted(FIGURES_DIR.glob('*.png')):
        fname = fig_path.name
        parts = fname.replace('.png', '').split('_')

        case_id = '_'.join(parts[:-1])
        fig_type = parts[-1] if parts else 'unknown'

        # Try to find actual case_id
        for d in RESULTS_DIR.iterdir():
            if d.is_dir() and fname.startswith(d.name):
                case_id = d.name
                fig_type = fname[len(d.name)+1:].replace('.png', '')
                break

        variable_map = {
            'residual': 'residual_l2',
            'forces': 'cd,cl',
            'cp': 'cp',
            'cf': 'cf',
            'mach': 'Mach',
            'pressure': 'Pressure',
            'velocityx': 'VelocityX',
        }

        source_map = {
            'residual': 'residuals.csv',
            'forces': 'forces.csv',
            'cp': 'surface.csv',
            'cf': 'surface.csv',
        }

        base_type = fig_type.replace('_zoom', '').lower()
        variable = variable_map.get(base_type, base_type)
        source = source_map.get(base_type, 'field_final.vtk')

        caption = f'{variable} for {case_id}'
        if 'zoom' in fig_type:
            caption += ' (near-body zoom)'

        manifest.append({
            'figure_file': f'figures/{fname}',
            'case_id': case_id,
            'figure_type': fig_type,
            'variable': variable,
            'source_file': f'results/{case_id}/{source}',
            'caption': caption,
        })

    manifest_path = SOLVER_DIR / 'report' / 'figure_manifest.csv'
    with open(manifest_path, 'w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=['figure_file', 'case_id', 'figure_type', 'variable', 'source_file', 'caption'])
        writer.writeheader()
        writer.writerows(manifest)

    print(f"Figure manifest written to {manifest_path}")


def generate_sanity_checks():
    """Generate machine-readable sanity checks."""
    checks = {}
    for case_dir in sorted(RESULTS_DIR.iterdir()):
        if not case_dir.is_dir():
            continue
        case_id = case_dir.name
        case_checks = {'case_id': case_id, 'passed': True, 'issues': []}

        # Check VTK field
        vtk_file = case_dir / 'field_final.vtk'
        if vtk_file.exists():
            try:
                _, _, _, cell_data = read_vtk_field(vtk_file)
                if 'Density' in cell_data:
                    rho = cell_data['Density']
                    if np.any(rho[np.isfinite(rho)] <= 0):
                        case_checks['issues'].append('negative_density')
                        case_checks['passed'] = False
                if 'Pressure' in cell_data:
                    p = cell_data['Pressure']
                    if np.any(p[np.isfinite(p)] <= 0):
                        case_checks['issues'].append('negative_pressure')
                        case_checks['passed'] = False
            except Exception as e:
                case_checks['issues'].append(f'vtk_read_error: {str(e)}')

        # Check forces
        forces_file = case_dir / 'forces.csv'
        if forces_file.exists():
            data = read_csv(forces_file)
            if 'cd' in data and len(data['cd']) > 0:
                final_cd = data['cd'][-1]
                final_cl = data['cl'][-1]

                if 'naca' in case_id and np.abs(final_cl) > 0.5:
                    case_checks['issues'].append(f'high_cl_at_zero_aoa: {final_cl:.4f}')

                if 'cylinder' in case_id and final_cd < 0:
                    case_checks['issues'].append(f'negative_cylinder_drag: {final_cd:.4f}')
                    case_checks['passed'] = False

        # Check surface for no-slip
        surface_file = case_dir / 'surface.csv'
        if surface_file.exists() and ('laminar' in case_id or 're20' in case_id or 're200' in case_id):
            data = read_csv(surface_file)
            if 'u' in data and 'v' in data:
                wall_vel = np.sqrt(data['u']**2 + data['v']**2)
                if np.mean(wall_vel) > 0.01:
                    case_checks['issues'].append(f'wall_velocity_not_zero: mean={np.mean(wall_vel):.4f}')

        checks[case_id] = case_checks

    sanity_path = SOLVER_DIR / 'report' / 'sanity_checks.json'
    with open(sanity_path, 'w') as f:
        json.dump(checks, f, indent=2)
    print(f"Sanity checks written to {sanity_path}")


def generate_run_manifest():
    """Generate run manifest from result directories."""
    rows = []
    for case_dir in sorted(RESULTS_DIR.iterdir()):
        if not case_dir.is_dir():
            continue
        status_file = case_dir / 'run_status.json'
        if not status_file.exists():
            continue
        with open(status_file) as f:
            status = json.load(f)

        rows.append({
            'case_id': status.get('case_id', case_dir.name),
            'mpi_ranks': status.get('mpi_ranks', '?'),
            'command': status.get('command', '?'),
            'wall_time_seconds': f"{status.get('wall_time_seconds', 0):.1f}",
            'final_step': status.get('final_step', '?'),
            'convergence_status': status.get('convergence_status', '?'),
            'residual_reduction': f"{status.get('residual_reduction_orders', 0):.2f}",
        })

    manifest_path = SOLVER_DIR / 'report' / 'run_manifest.csv'
    with open(manifest_path, 'w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=[
            'case_id', 'mpi_ranks', 'command', 'wall_time_seconds',
            'final_step', 'convergence_status', 'residual_reduction'])
        writer.writeheader()
        writer.writerows(rows)
    print(f"Run manifest written to {manifest_path}")


if __name__ == '__main__':
    FIGURES_DIR.mkdir(parents=True, exist_ok=True)

    ALL_CASES = [
        'naca0012_m015_inviscid',
        'naca0012_m080_inviscid',
        'naca0012_m200_inviscid',
        'naca0012_m015_laminar_re5000',
        'naca0012_m080_laminar_re5000',
        'naca0012_m200_laminar_re5000',
        'cylinder_m010_laminar_re20',
        'cylinder_m010_laminar_re200',
        'naca0012_m015_inviscid_np8',
        'cylinder_m010_laminar_re20_np8',
    ]

    for case_id in ALL_CASES:
        try:
            process_case(case_id)
        except Exception as e:
            print(f"ERROR processing {case_id}: {e}")

    generate_figure_manifest()
    generate_sanity_checks()
    generate_run_manifest()

    print("\nAll plots generated.")
