#!/usr/bin/env python3
"""CFD Solver Benchmark Report Generator"""

import json, os, csv, sys
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.tri as mtri
import xml.etree.ElementTree as ET

RESULTS_DIR = "/workspace/solver/results"
REPORT_DIR = "/workspace/solver/report"
FIGURES_DIR = f"{REPORT_DIR}/figures"
GIT_HASH = "20c70b26f1ec07cda84c494228ca7052045439f3"
BINARY = "/workspace/solver/build/cfd_solver"

os.makedirs(FIGURES_DIR, exist_ok=True)

COMPLETED_CASES = [
    "naca0012_m015_inviscid",
    "naca0012_m080_inviscid",
    "naca0012_m200_inviscid",
]

ALL_CASES = [
    "naca0012_m015_inviscid",
    "naca0012_m080_inviscid",
    "naca0012_m200_inviscid",
    "naca0012_m015_laminar_re5000",
    "naca0012_m080_laminar_re5000",
    "naca0012_m200_laminar_re5000",
    "cylinder_m010_laminar_re20",
    "cylinder_m010_laminar_re200",
]

status_log = []

def parse_vtu(vtu_path):
    tree = ET.parse(vtu_path)
    root = tree.getroot()
    piece = root.find('.//Piece')
    n_points = int(piece.get('NumberOfPoints'))
    n_cells = int(piece.get('NumberOfCells'))
    data = {}
    for da in piece.find('Points').findall('DataArray'):
        raw = ' '.join(da.text.split())
        arr = np.fromstring(raw, dtype=float, sep=' ')
        data['points'] = arr.reshape(n_points, 3)
    cells_elem = piece.find('Cells')
    connectivity = None
    offsets = None
    for da in cells_elem.findall('DataArray'):
        name = da.get('Name')
        raw = ' '.join(da.text.split())
        if name == 'connectivity':
            connectivity = np.fromstring(raw, dtype=int, sep=' ')
        elif name == 'offsets':
            offsets = np.fromstring(raw, dtype=int, sep=' ')
    cells_list = []
    prev = 0
    for off in offsets:
        cells_list.append(connectivity[prev:off])
        prev = off
    data['cells'] = cells_list
    pts = data['points'][:, :2]
    centers = np.array([pts[c].mean(axis=0) for c in cells_list])
    data['cell_centers'] = centers
    cd = piece.find('CellData')
    if cd is not None:
        for da in cd.findall('DataArray'):
            name = da.get('Name')
            ncomp_str = da.get('NumberOfComponents')
            ncomp = int(ncomp_str) if ncomp_str else 1
            raw = ' '.join(da.text.split())
            arr = np.fromstring(raw, dtype=float, sep=' ')
            if ncomp > 1:
                data[name] = arr.reshape(n_cells, ncomp)
            else:
                data[name] = arr
    return data

def plot_field(case_id, field_data, field_name, title, cmap, filename):
    centers = field_data['cell_centers']
    values = field_data[field_name]
    if values.ndim > 1:
        values = np.linalg.norm(values[:, :2], axis=1)
    x = centers[:, 0]
    y = centers[:, 1]
    z = values
    mask = (x > -1.5) & (x < 3.0) & (y > -1.5) & (y < 1.5)
    x_m, y_m, z_m = x[mask], y[mask], z[mask]
    fig, ax = plt.subplots(figsize=(10, 6))
    try:
        triang = mtri.Triangulation(x_m, y_m)
        tri_pts = np.stack([x_m[triang.triangles], y_m[triang.triangles]], axis=-1)
        edge_lens = []
        for i in range(3):
            j = (i+1) % 3
            d = np.linalg.norm(tri_pts[:, i] - tri_pts[:, j], axis=-1)
            edge_lens.append(d)
        max_edge = np.max(edge_lens, axis=0)
        triang.set_mask(max_edge > 0.3)
        vmin, vmax = np.percentile(z_m, 2), np.percentile(z_m, 98)
        levels = np.linspace(vmin, vmax, 50)
        cf = ax.tricontourf(triang, z_m, levels=levels, cmap=cmap, extend='both')
        plt.colorbar(cf, ax=ax, label=title)
    except Exception as e:
        sc = ax.scatter(x_m, y_m, c=z_m, cmap=cmap, s=1, rasterized=True)
        plt.colorbar(sc, ax=ax, label=title)
    ax.set_aspect('equal')
    ax.set_xlabel('x/c')
    ax.set_ylabel('y/c')
    ax.set_title(f'{title} — {case_id.replace("_", " ")}')
    plt.tight_layout()
    plt.savefig(filename, dpi=100, bbox_inches='tight')
    plt.close()

def plot_residuals(case_id, res_path, filename):
    steps, residuals = [], []
    with open(res_path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            steps.append(int(row['step']))
            residuals.append(float(row['residual_l2']))
    fig, ax = plt.subplots(figsize=(8, 5))
    ax.semilogy(steps, residuals, 'b-', linewidth=1.2)
    ax.set_xlabel('Iteration')
    ax.set_ylabel('L2 Residual')
    ax.set_title(f'Convergence History — {case_id.replace("_", " ")}')
    ax.grid(True, alpha=0.4)
    plt.tight_layout()
    plt.savefig(filename, dpi=100, bbox_inches='tight')
    plt.close()

def plot_forces(case_id, forces_path, filename):
    steps, cl_vals, cd_vals = [], [], []
    with open(forces_path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            steps.append(int(row['step']))
            cl_vals.append(float(row['cl']))
            cd_vals.append(float(row['cd']))
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(8, 7), sharex=True)
    ax1.plot(steps, cd_vals, 'r-', linewidth=1.2)
    ax1.set_ylabel('C_d')
    ax1.set_title(f'Force History — {case_id.replace("_", " ")}')
    ax1.grid(True, alpha=0.4)
    ax2.plot(steps, cl_vals, 'b-', linewidth=1.2)
    ax2.set_ylabel('C_l')
    ax2.set_xlabel('Iteration')
    ax2.grid(True, alpha=0.4)
    plt.tight_layout()
    plt.savefig(filename, dpi=100, bbox_inches='tight')
    plt.close()

figure_rows = []
sanity_cases = {}

for case_id in COMPLETED_CASES:
    case_dir = f"{RESULTS_DIR}/{case_id}"
    vtu_path = f"{case_dir}/field_final_rank0.vtu"
    res_path = f"{case_dir}/residuals.csv"
    forces_path = f"{case_dir}/forces.csv"
    print(f"Processing {case_id}...")
    try:
        fd = parse_vtu(vtu_path)
        print(f"  VTU parsed: {len(fd['cell_centers'])} cells")
    except Exception as e:
        print(f"  ERROR parsing VTU: {e}")
        status_log.append(f"ERROR: VTU parse failed for {case_id}: {e}")
        fd = None
    mach_fig = f"{FIGURES_DIR}/{case_id}_mach.png"
    if fd is not None and 'mach' in fd:
        try:
            plot_field(case_id, fd, 'mach', 'Mach Number', 'RdBu_r', mach_fig)
            status_log.append(f"OK: {case_id}_mach.png generated")
            figure_rows.append({'figure_file': f"figures/{case_id}_mach.png",'case_id': case_id,'figure_type': 'field','variable': 'mach','source_file': f"results/{case_id}/field_final_rank0.vtu",'caption': f"Mach number field for {case_id.replace('_', ' ')} case"})
        except Exception as e:
            status_log.append(f"ERROR: mach figure failed for {case_id}: {e}")
    pressure_fig = f"{FIGURES_DIR}/{case_id}_pressure.png"
    if fd is not None and 'pressure' in fd:
        try:
            plot_field(case_id, fd, 'pressure', 'Pressure (p/p_inf)', 'viridis', pressure_fig)
            status_log.append(f"OK: {case_id}_pressure.png generated")
            figure_rows.append({'figure_file': f"figures/{case_id}_pressure.png",'case_id': case_id,'figure_type': 'field','variable': 'pressure','source_file': f"results/{case_id}/field_final_rank0.vtu",'caption': f"Pressure field for {case_id.replace('_', ' ')} case"})
        except Exception as e:
            status_log.append(f"ERROR: pressure figure failed for {case_id}: {e}")
    res_fig = f"{FIGURES_DIR}/{case_id}_residuals.png"
    try:
        plot_residuals(case_id, res_path, res_fig)
        status_log.append(f"OK: {case_id}_residuals.png generated")
        figure_rows.append({'figure_file': f"figures/{case_id}_residuals.png",'case_id': case_id,'figure_type': 'convergence','variable': 'residual_l2','source_file': f"results/{case_id}/residuals.csv",'caption': f"Residual convergence for {case_id.replace('_', ' ')}"})
    except Exception as e:
        status_log.append(f"ERROR: residuals figure failed for {case_id}: {e}")
    forces_fig = f"{FIGURES_DIR}/{case_id}_forces.png"
    try:
        plot_forces(case_id, forces_path, forces_fig)
        status_log.append(f"OK: {case_id}_forces.png generated")
        figure_rows.append({'figure_file': f"figures/{case_id}_forces.png",'case_id': case_id,'figure_type': 'forces','variable': 'Cd/Cl','source_file': f"results/{case_id}/forces.csv",'caption': f"Force history for {case_id.replace('_', ' ')}"})
    except Exception as e:
        status_log.append(f"ERROR: forces figure failed for {case_id}: {e}")
    try:
        with open(f"{case_dir}/run_status.json") as f:
            rs = json.load(f)
        final_residual = None
        with open(res_path) as f:
            for row in csv.DictReader(f):
                pass
            final_residual = float(row['residual_l2'])
        Cd, Cl = None, None
        with open(forces_path) as f:
            for row in csv.DictReader(f):
                pass
            Cd = float(row['cd'])
            Cl = float(row['cl'])
        sanity_cases[case_id] = {"final_residual": final_residual,"Cd": Cd,"Cl": Cl,"convergence_orders": rs['residual_reduction_orders'],"status": rs['convergence_status']}
    except Exception as e:
        print(f"  ERROR building sanity data: {e}")

PARTIAL_CASES = ["naca0012_m015_laminar_re5000","naca0012_m080_laminar_re5000","naca0012_m200_laminar_re5000","cylinder_m010_laminar_re20","cylinder_m010_laminar_re200"]
for case_id in PARTIAL_CASES:
    case_dir = f"{RESULTS_DIR}/{case_id}"
    res_path = f"{case_dir}/residuals.csv"
    forces_path = f"{case_dir}/forces.csv"
    if not os.path.exists(res_path):
        continue
    res_fig = f"{FIGURES_DIR}/{case_id}_residuals.png"
    try:
        plot_residuals(case_id, res_path, res_fig)
        status_log.append(f"OK: {case_id}_residuals.png generated (partial)")
        figure_rows.append({'figure_file': f"figures/{case_id}_residuals.png",'case_id': case_id,'figure_type': 'convergence','variable': 'residual_l2','source_file': f"results/{case_id}/residuals.csv",'caption': f"Residual convergence for {case_id.replace('_', ' ')} (partial)"})
    except Exception as e:
        status_log.append(f"ERROR: residuals failed for {case_id}: {e}")
    if os.path.exists(forces_path):
        forces_fig = f"{FIGURES_DIR}/{case_id}_forces.png"
        try:
            plot_forces(case_id, forces_path, forces_fig)
            status_log.append(f"OK: {case_id}_forces.png generated (partial)")
            figure_rows.append({'figure_file': f"figures/{case_id}_forces.png",'case_id': case_id,'figure_type': 'forces','variable': 'Cd/Cl','source_file': f"results/{case_id}/forces.csv",'caption': f"Force history for {case_id.replace('_', ' ')} (partial)"})
        except Exception as e:
            status_log.append(f"ERROR: forces failed for {case_id}: {e}")
    try:
        final_residual = None
        with open(res_path) as f:
            for row in csv.DictReader(f):
                pass
            final_residual = float(row['residual_l2'])
        Cd, Cl = None, None
        if os.path.exists(forces_path):
            with open(forces_path) as f:
                for row in csv.DictReader(f):
                    pass
                Cd = float(row['cd'])
                Cl = float(row['cl'])
        sanity_cases[case_id] = {"final_residual": final_residual,"Cd": Cd,"Cl": Cl,"convergence_orders": None,"status": "running"}
    except Exception as e:
        print(f"  ERROR sanity for {case_id}: {e}")

print("Writing manifests...")
manifest_rows = []
for case_id in ALL_CASES:
    case_dir = f"{RESULTS_DIR}/{case_id}"
    rs_path = f"{case_dir}/run_status.json"
    if os.path.exists(rs_path):
        with open(rs_path) as f:
            rs = json.load(f)
        manifest_rows.append({'case_id': case_id,'np': rs.get('mpi_ranks',1),'final_step': rs.get('final_step','N/A'),'convergence_status': rs.get('convergence_status','unknown'),'residual_reduction': round(rs.get('residual_reduction_orders',0),2),'wall_time_s': round(rs.get('wall_time_seconds',0),1),'binary': BINARY,'git_commit': GIT_HASH[:12]})
    else:
        res_path = f"{case_dir}/residuals.csv"
        final_step = 'N/A'
        if os.path.exists(res_path):
            with open(res_path) as f:
                for row in csv.DictReader(f):
                    pass
                try:
                    final_step = int(row['step'])
                except:
                    pass
        manifest_rows.append({'case_id': case_id,'np': 1,'final_step': final_step,'convergence_status': 'running','residual_reduction': 'N/A','wall_time_s': 'N/A','binary': BINARY,'git_commit': GIT_HASH[:12]})

with open(f"{REPORT_DIR}/run_manifest.csv", 'w', newline='') as f:
    writer = csv.DictWriter(f, fieldnames=['case_id','np','final_step','convergence_status','residual_reduction','wall_time_s','binary','git_commit'])
    writer.writeheader()
    writer.writerows(manifest_rows)
status_log.append("OK: run_manifest.csv written")

sanity = {"timestamp": "2026-08-27","cases": sanity_cases}
with open(f"{REPORT_DIR}/sanity_checks.json", 'w') as f:
    json.dump(sanity, f, indent=2)
status_log.append("OK: sanity_checks.json written")

with open(f"{REPORT_DIR}/figure_manifest.csv", 'w', newline='') as f:
    writer = csv.DictWriter(f, fieldnames=['figure_file','case_id','figure_type','variable','source_file','caption'])
    writer.writeheader()
    writer.writerows(figure_rows)
status_log.append("OK: figure_manifest.csv written")

status_log.append("OK: report.tex to be written separately")

generated_figs = [s for s in status_log if s.startswith("OK:") and ".png" in s]
errors = [s for s in status_log if s.startswith("ERROR:")]

with open(f"{REPORT_DIR}/report_status.txt", 'w') as f:
    f.write("=== CFD Benchmark Report Generation Status ===
")
    f.write("Generated: 2026-08-27

")
    f.write(f"Successfully generated figures ({len(generated_figs)}):
")
    for s in generated_figs:
        f.write(f"  {s}
")
    f.write(f"
Errors ({len(errors)}):
")
    for e in errors:
        f.write(f"  {e}
")
    f.write(f"
Other artifacts:
")
    for s in status_log:
        if not s.startswith("OK:") or ".png" not in s:
            f.write(f"  {s}
")
    f.write("
=== Approximate Completion ===
")
    f.write(f"Figures: {len(generated_figs)} generated
")
    f.write("run_manifest.csv: DONE
")
    f.write("sanity_checks.json: DONE
")
    f.write("figure_manifest.csv: DONE
")
    f.write("report.tex: PENDING (separate script)
")

print("
=== DONE ===")
for s in status_log:
    print(f"  {s}")
