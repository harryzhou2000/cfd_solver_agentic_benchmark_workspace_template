import os
import sys
import math
import xml.etree.ElementTree as ET
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import csv
import json

RESULTS_DIR = '/workspace/solver/results'
REPORT_DIR = '/workspace/solver/report'
FIGURES_DIR = os.path.join(REPORT_DIR, 'figures')
os.makedirs(FIGURES_DIR, exist_ok=True)

def parse_vtu(vtu_path):
    tree = ET.parse(vtu_path)
    root = tree.getroot()
    piece = root.find('.//Piece')
    n_points = int(piece.attrib['NumberOfPoints'])
    n_cells = int(piece.attrib['NumberOfCells'])
    points_da = piece.find('.//Points/DataArray')
    pts_flat = list(map(float, points_da.text.split()))
    pts = np.array(pts_flat).reshape(n_points, 3)
    coords = pts[:, :2]
    cells_elem = piece.find('.//Cells')
    conn_text = offsets_text = None
    for da in cells_elem.findall('DataArray'):
        nm = da.attrib.get('Name','')
        if nm == 'connectivity': conn_text = da.text
        elif nm == 'offsets': offsets_text = da.text
    connectivity = list(map(int, conn_text.split()))
    offsets = list(map(int, offsets_text.split()))
    prev = 0
    cell_centers = np.zeros((n_cells, 2))
    for i, off in enumerate(offsets):
        verts = connectivity[prev:off]
        cell_centers[i] = coords[verts].mean(axis=0)
        prev = off
    fields = {}
    cd_elem = piece.find('.//CellData')
    if cd_elem is not None:
        for da in cd_elem.findall('DataArray'):
            nm = da.attrib.get('Name','')
            nc = int(da.attrib.get('NumberOfComponents','1'))
            vals = list(map(float, da.text.split()))
            if nc == 1:
                fields[nm] = np.array(vals)
            else:
                fields[nm] = np.array(vals).reshape(-1, nc)
    return cell_centers, fields

def read_pvtu(pvtu_path):
    pvtu_dir = os.path.dirname(pvtu_path)
    tree = ET.parse(pvtu_path)
    root = tree.getroot()
    pieces = root.findall('.//Piece')
    all_centers = []
    all_fields = {}
    for piece in pieces:
        src = piece.attrib.get('Source','')
        vtu_file = os.path.join(pvtu_dir, src)
        if not os.path.exists(vtu_file):
            continue
        centers, fields = parse_vtu(vtu_file)
        all_centers.append(centers)
        for k, v in fields.items():
            all_fields.setdefault(k, []).append(v)
    if not all_centers:
        return None, None
    centers = np.concatenate(all_centers, axis=0)
    combined = {k: np.concatenate(v, axis=0) for k, v in all_fields.items()}
    return centers, combined

def plot_field(case, pvtu_path, field_name, fig_filename, title, cmap='turbo', clip=None, xlim=None, ylim=None):
    centers, fields = read_pvtu(pvtu_path)
    if centers is None or field_name not in fields:
        print(f"  Warning: {field_name} not in {pvtu_path}")
        return False
    data = fields[field_name]
    if data.ndim > 1:
        data = np.sqrt(data[:,0]**2 + data[:,1]**2)
    if clip is not None:
        data = np.clip(data, clip[0], clip[1])
    x = centers[:, 0]
    y = centers[:, 1]
    if xlim:
        mask = (x >= xlim[0]) & (x <= xlim[1])
        if ylim:
            mask &= (y >= ylim[0]) & (y <= ylim[1])
        x, y, data = x[mask], y[mask], data[mask]
    sz = 0.5 if len(x) > 30000 else 2.0
    fig, ax = plt.subplots(figsize=(10, 6))
    sc = ax.scatter(x, y, c=data, s=sz, cmap=cmap, rasterized=True)
    plt.colorbar(sc, ax=ax, label=field_name)
    ax.set_aspect('equal')
    ax.set_title(title)
    ax.set_xlabel('x')
    ax.set_ylabel('y')
    if xlim: ax.set_xlim(xlim)
    if ylim: ax.set_ylim(ylim)
    out_path = os.path.join(FIGURES_DIR, fig_filename)
    fig.savefig(out_path, dpi=130, bbox_inches='tight')
    plt.close(fig)
    print(f"  Written: {out_path}")
    return True

def plot_vorticity_re200(pvtu_path, fig_filename):
    centers, fields = read_pvtu(pvtu_path)
    if centers is None or 'velocity' not in fields:
        return False
    from scipy.spatial import KDTree
    vel = fields['velocity']
    u = vel[:, 0]; v_y = vel[:, 1]
    x = centers[:, 0]; y = centers[:, 1]
    # Focus on wake region
    mask = (x > -2) & (x < 12) & (y > -5) & (y < 5)
    xi, yi, ui, vi = x[mask], y[mask], u[mask], v_y[mask]
    tree = KDTree(np.column_stack([xi, yi]))
    k = 8
    _, idx = tree.query(np.column_stack([xi, yi]), k=k+1)
    vort = np.zeros(len(xi))
    for i in range(len(xi)):
        nbrs = idx[i, 1:]
        dx = xi[nbrs] - xi[i]; dy = yi[nbrs] - yi[i]
        du = ui[nbrs] - ui[i]; dv = vi[nbrs] - vi[i]
        A = np.column_stack([dx, dy])
        try:
            gu, _, _, _ = np.linalg.lstsq(A, du, rcond=None)
            gv, _, _, _ = np.linalg.lstsq(A, dv, rcond=None)
            vort[i] = gv[0] - gu[1]
        except:
            vort[i] = 0.0
    clip_val = 5.0
    vort_c = np.clip(vort, -clip_val, clip_val)
    fig, ax = plt.subplots(figsize=(12, 5))
    sc = ax.scatter(xi, yi, c=vort_c, s=1.0, cmap='RdBu_r', vmin=-clip_val, vmax=clip_val, rasterized=True)
    plt.colorbar(sc, ax=ax, label='Vorticity (clipped ±5)')
    ax.set_aspect('equal')
    ax.set_xlim(-2, 12)
    ax.set_ylim(-5, 5)
    ax.set_title('Vorticity Field — cylinder_m010_laminar_re200')
    ax.set_xlabel('x'); ax.set_ylabel('y')
    out_path = os.path.join(FIGURES_DIR, fig_filename)
    fig.savefig(out_path, dpi=130, bbox_inches='tight')
    plt.close(fig)
    print(f"  Written: {out_path}")
    return True

def plot_residuals(case, csv_path, fig_filename):
    steps, res_l2 = [], []
    with open(csv_path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            try:
                steps.append(float(row['step']))
                res_l2.append(float(row['residual_l2']))
            except: pass
    if not steps: return False
    steps = np.array(steps); res_l2 = np.array(res_l2)
    res0 = res_l2[0] if res_l2[0] > 0 else 1.0
    fig, ax = plt.subplots(figsize=(8,5))
    ax.semilogy(steps, res_l2 / res0, 'b-', linewidth=1)
    ax.set_xlabel('Step'); ax.set_ylabel('Normalized Residual L2')
    ax.set_title(f'Convergence: {case}')
    ax.grid(True, which='both', alpha=0.3)
    out_path = os.path.join(FIGURES_DIR, fig_filename)
    fig.savefig(out_path, dpi=120, bbox_inches='tight')
    plt.close(fig)
    print(f"  Written: {out_path}")
    return True

def plot_forces(case, forces_csv, fig_filename):
    times, cl_vals, cd_vals = [], [], []
    with open(forces_csv) as f:
        reader = csv.DictReader(f)
        for row in reader:
            try:
                times.append(float(row['physical_time']))
                cl_vals.append(float(row['cl']))
                cd_vals.append(float(row['cd']))
            except: pass
    if not times: return False
    times = np.array(times); cl_vals = np.array(cl_vals); cd_vals = np.array(cd_vals)
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 8), sharex=True)
    ax1.plot(times, cl_vals, 'b-', linewidth=0.8); ax1.set_ylabel('CL'); ax1.grid(True, alpha=0.3)
    ax2.plot(times, cd_vals, 'r-', linewidth=0.8); ax2.set_ylabel('CD'); ax2.set_xlabel('Physical Time'); ax2.grid(True, alpha=0.3)
    fig.suptitle(f'Force History: {case}')
    out_path = os.path.join(FIGURES_DIR, fig_filename)
    fig.savefig(out_path, dpi=120, bbox_inches='tight')
    plt.close(fig)
    print(f"  Written: {out_path}")
    return True

CASES = [
    'naca0012_m015_inviscid',
    'naca0012_m080_inviscid',
    'naca0012_m200_inviscid',
    'naca0012_m015_laminar_re5000',
    'naca0012_m080_laminar_re5000',
    'naca0012_m200_laminar_re5000',
    'cylinder_m010_laminar_re200',
    'cylinder_m010_laminar_re20',
]

manifest_entries = []

for case in CASES:
    case_dir = os.path.join(RESULTS_DIR, case)
    pvtu = os.path.join(case_dir, 'field_final.pvtu')
    if not os.path.exists(pvtu):
        print(f"Skipping {case} - no field_final.pvtu")
        continue
    print(f"\nProcessing {case}...")
    xlim = (-0.3, 1.5) if 'naca' in case else (-3, 10)
    ylim = (-0.8, 0.8) if 'naca' in case else (-5, 5)
    mach_fig = f'{case}_mach.png'
    if plot_field(case, pvtu, 'mach', mach_fig, f'Mach — {case}', 'turbo', xlim=xlim, ylim=ylim):
        manifest_entries.append({'figure_file': mach_fig, 'case_id': case, 'figure_type': 'field', 'variable': 'mach', 'source_file': 'field_final.pvtu', 'caption': f'Mach number field for {case}'})
    pres_fig = f'{case}_pressure.png'
    if plot_field(case, pvtu, 'pressure', pres_fig, f'Pressure — {case}', 'RdBu_r', xlim=xlim, ylim=ylim):
        manifest_entries.append({'figure_file': pres_fig, 'case_id': case, 'figure_type': 'field', 'variable': 'pressure', 'source_file': 'field_final.pvtu', 'caption': f'Pressure field for {case}'})
    res_csv = os.path.join(case_dir, 'residuals.csv')
    if os.path.exists(res_csv):
        res_fig = f'{case}_residuals.png'
        if plot_residuals(case, res_csv, res_fig):
            manifest_entries.append({'figure_file': res_fig, 'case_id': case, 'figure_type': 'convergence', 'variable': 'residual', 'source_file': 'residuals.csv', 'caption': f'Residual convergence for {case}'})
    if 're200' in case:
        vort_fig = f'{case}_vorticity.png'
        if plot_vorticity_re200(pvtu, vort_fig):
            manifest_entries.append({'figure_file': vort_fig, 'case_id': case, 'figure_type': 'field', 'variable': 'vorticity', 'source_file': 'field_final.pvtu', 'caption': f'Vorticity field for {case}'})
        forces_csv = os.path.join(case_dir, 'forces.csv')
        if os.path.exists(forces_csv):
            forces_fig = f'{case}_forces.png'
            if plot_forces(case, forces_csv, forces_fig):
                manifest_entries.append({'figure_file': forces_fig, 'case_id': case, 'figure_type': 'forces', 'variable': 'lift_drag', 'source_file': 'forces.csv', 'caption': f'Force history for {case}'})

with open(os.path.join(REPORT_DIR, 'figure_manifest.json'), 'w') as f:
    json.dump(manifest_entries, f, indent=2)
print(f"\nDone: {len(manifest_entries)} figure entries written.")
