#!/usr/bin/env python3
"""Generate all required figures for the CFD benchmark report."""
import os, sys, csv, json
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import pyvista as pv

RESULTS = os.path.join(os.path.dirname(__file__), '..', 'results')
FIGDIR = os.path.join(os.path.dirname(__file__), '..', 'report', 'figures')
os.makedirs(FIGDIR, exist_ok=True)

plt.rcParams.update({'font.size': 10, 'figure.dpi': 150, 'savefig.dpi': 150,
                      'axes.grid': True, 'grid.alpha': 0.3})

CASES = [
    'naca0012_m015_inviscid', 'naca0012_m080_inviscid', 'naca0012_m200_inviscid',
    'naca0012_m015_laminar_re5000', 'naca0012_m080_laminar_re5000', 'naca0012_m200_laminar_re5000',
    'cylinder_m010_laminar_re20', 'cylinder_m010_laminar_re200',
]

def read_csv(path):
    rows = []
    if not os.path.exists(path): return rows
    with open(path) as f:
        reader = csv.DictReader(f)
        for r in reader: rows.append(r)
    return rows

def plot_residuals(case_id):
    rows = read_csv(os.path.join(RESULTS, case_id, 'residuals.csv'))
    if not rows: return
    steps = [int(float(r['step'])) for r in rows]
    l2 = [float(r['residual_l2']) for r in rows]
    linf = [float(r['residual_linf']) for r in rows]
    fig, ax = plt.subplots(figsize=(8, 4))
    ax.semilogy(steps, l2, label='L2', linewidth=1)
    ax.semilogy(steps, linf, label='Linf', linewidth=1)
    ax.set_xlabel('Step'); ax.set_ylabel('Residual')
    ax.set_title(f'{case_id} - Residual History')
    ax.legend()
    fig.tight_layout(); fig.savefig(os.path.join(FIGDIR, f'{case_id}_residuals.png')); plt.close()

def plot_forces(case_id):
    rows = read_csv(os.path.join(RESULTS, case_id, 'forces.csv'))
    if not rows: return
    steps = [int(float(r['step'])) for r in rows]
    t = [float(r['physical_time']) for r in rows]
    cl = [float(r['cl']) for r in rows]
    cd = [float(r['cd']) for r in rows]
    xlabel = 'Physical Time' if max(t) > 0 else 'Step'
    xval = t if max(t) > 0 else steps
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(8, 6))
    ax1.plot(xval, cl, linewidth=0.5); ax1.set_ylabel('Cl'); ax1.set_title(f'{case_id} - Force History')
    ax2.plot(xval, cd, linewidth=0.5); ax2.set_ylabel('Cd'); ax2.set_xlabel(xlabel)
    fig.tight_layout(); fig.savefig(os.path.join(FIGDIR, f'{case_id}_forces.png')); plt.close()

def plot_surface(case_id):
    rows = read_csv(os.path.join(RESULTS, case_id, 'surface.csv'))
    if not rows: return
    x = [float(r['x']) for r in rows]
    y = [float(r['y']) for r in rows]
    cp = [float(r['cp']) for r in rows]
    fig, ax = plt.subplots(figsize=(8, 4))
    ax.plot(x, cp, 'b.', markersize=2)
    ax.set_xlabel('x'); ax.set_ylabel('Cp')
    ax.set_title(f'{case_id} - Surface Pressure Coefficient')
    ax.invert_yaxis()
    fig.tight_layout(); fig.savefig(os.path.join(FIGDIR, f'{case_id}_surface_cp.png')); plt.close()

def plot_field(case_id, var, clip=None):
    vtk_path = os.path.join(RESULTS, case_id, 'field_final.vtk')
    if not os.path.exists(vtk_path): return
    mesh = pv.read(vtk_path)
    if var not in mesh.cell_data: return
    data = mesh.cell_data[var]
    if clip:
        data = np.clip(data, clip[0], clip[1])
    fig, ax = plt.subplots(figsize=(10, 6))
    mesh.plot_scalars(data, scalars=var, show_edges=False, cmap='jet',
                      clim=[np.nanpercentile(data, 2), np.nanpercentile(data, 98)] if not clip else clip,
                      title=f'{case_id} - {var}', show_scalar_bar=True,
                      screenshot=os.path.join(FIGDIR, f'{case_id}_{var}.png'),
                      window_size=[1000, 600], off_screen=True, cpos='xy')
    plt.close('all')

def plot_field_contour(case_id, var, clip=None, zoom=None):
    vtk_path = os.path.join(RESULTS, case_id, 'field_final.vtk')
    if not os.path.exists(vtk_path): return
    mesh = pv.read(vtk_path)
    # Compute vorticity from velocity if not present
    if var == 'vorticity' and var not in mesh.cell_data:
        if 'velocity' in mesh.cell_data:
            # Compute vorticity using cell gradients via pyvista
            mesh['vel'] = mesh.cell_data['velocity']
            mesh = mesh.compute_derivative(scalars='vel', gradient=True)
            grad = mesh.cell_data['gradient']
            # vorticity z = du/dy - dv/dx (indices: u_x=0, u_y=1, v_x=3, v_y=4)
            data = grad[:, 1] - grad[:, 3]
        else:
            return
    elif var not in mesh.cell_data:
        return
    else:
        data = mesh.cell_data[var]
    data = np.clip(data, clip[0] if clip else -1e30, clip[1] if clip else 1e30)
    mesh.cell_data[var] = data
    xmin, xmax, ymin, ymax = mesh.bounds[0], mesh.bounds[1], mesh.bounds[2], mesh.bounds[3]
    if zoom:
        xmin, xmax, ymin, ymax = zoom
    fig, ax = plt.subplots(figsize=(10, 6))
    # Use matplotlib tricontourf for publication-quality contours
    pts = mesh.cell_centers().points
    x, y = pts[:, 0], pts[:, 1]
    from matplotlib.tri import Triangulation
    try:
        tri = Triangulation(x, y)
        levels = np.linspace(np.nanpercentile(data, 2), np.nanpercentile(data, 98), 50) if not clip else np.linspace(clip[0], clip[1], 50)
        tcf = ax.tricontourf(x, y, data, levels=levels, cmap='jet', extend='both')
        ax.set_xlim(xmin, xmax); ax.set_ylim(ymin, ymax)
        ax.set_aspect('equal')
        plt.colorbar(tcf, ax=ax, label=var)
        ax.set_xlabel('x'); ax.set_ylabel('y')
        ax.set_title(f'{case_id} - {var}')
    except Exception as e:
        ax.scatter(x, y, c=data, s=1, cmap='jet')
        ax.set_title(f'{case_id} - {var} (scatter)')
    fig.tight_layout()
    fig.savefig(os.path.join(FIGDIR, f'{case_id}_{var}.png'))
    plt.close()

def plot_mpi_comparison(base_case, var='cd'):
    fig, ax = plt.subplots(figsize=(8, 4))
    for np_val in [1, 2, 4, 8]:
        d = base_case if np_val == 1 else f'{base_case}_np{np_val}'
        rows = read_csv(os.path.join(RESULTS, d, 'forces.csv'))
        if not rows: continue
        steps = [int(float(r['step'])) for r in rows]
        cd = [float(r[var]) for r in rows]
        ax.plot(steps, cd, label=f'np={np_val}', linewidth=1)
    ax.set_xlabel('Step'); ax.set_ylabel(var)
    ax.set_title(f'{base_case} - MPI Rank Count Comparison ({var})')
    ax.legend()
    fig.tight_layout(); fig.savefig(os.path.join(FIGDIR, f'{base_case}_mpi_comparison.png')); plt.close()

def main():
    for case_id in CASES:
        d = os.path.join(RESULTS, case_id)
        if not os.path.exists(d): continue
        print(f'Plotting {case_id}...')
        plot_residuals(case_id)
        plot_forces(case_id)
        plot_surface(case_id)
        plot_field_contour(case_id, 'mach')
        plot_field_contour(case_id, 'pressure')
        if 're200' in case_id:
            # Wake vorticity visualization
            plot_field_contour(case_id, 'vorticity', clip=[-5, 5])
            # Also velocity magnitude zoom on wake
            plot_field_contour(case_id, 'mach', zoom=[-5, 15, -5, 5])
    # MPI comparisons
    plot_mpi_comparison('naca0012_m015_inviscid')
    plot_mpi_comparison('cylinder_m010_laminar_re20')
    print('All plots generated.')

if __name__ == '__main__':
    main()
