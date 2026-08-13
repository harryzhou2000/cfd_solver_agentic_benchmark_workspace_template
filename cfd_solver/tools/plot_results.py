#!/usr/bin/env python3
"""Generate visualizations from CFD solver output files."""
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.tri as tri
import csv, json, os, sys, argparse
from pathlib import Path

def read_csv(filename):
    rows = []
    with open(filename) as f:
        reader = csv.DictReader(f)
        for row in reader:
            d = {}
            for k, v in row.items():
                try:
                    d[k] = float(v)
                except (ValueError, TypeError):
                    d[k] = v
            rows.append(d)
    return rows

def read_csv_original_broken(filename):
    rows = []
    with open(filename) as f:
        reader = csv.DictReader(f)
        for row in reader:
            rows.append({k: float(v) if v.replace('.','',1).replace('-','',1).replace('e','',1).replace('-','',1).isdigit() else v for k,v in row.items()})
    return rows

def plot_residuals(result_dir, case_id, out_dir):
    res_file = os.path.join(result_dir, 'residuals.csv')
    if not os.path.exists(res_file): 
        print(f"Missing {res_file}")
        return False
    
    rows = read_csv(res_file)
    if not rows: return False
    
    steps = [int(r['step']) for r in rows]
    res = [r.get('residual_l2', 0) for r in rows]
    
    fig, ax = plt.subplots(figsize=(8, 4))
    ax.semilogy(steps, res, 'b-', linewidth=1)
    ax.set_xlabel('Iteration')
    ax.set_ylabel('L2 Residual')
    ax.set_title(f'{case_id} - Residual History')
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, f'{case_id}_residuals.png'), dpi=150)
    plt.close(fig)
    return True

def plot_forces(result_dir, case_id, out_dir):
    force_file = os.path.join(result_dir, 'forces.csv')
    if not os.path.exists(force_file): return False
    
    rows = read_csv(force_file)
    if not rows: return False
    
    steps = [int(r['step']) for r in rows]
    cd = [r.get('cd', 0) for r in rows]
    cl = [r.get('cl', 0) for r in rows]
    
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(8, 6), sharex=True)
    ax1.plot(steps, cd, 'r-', linewidth=1, label='CD')
    ax1.set_ylabel('Drag Coefficient')
    ax1.legend()
    ax1.grid(True, alpha=0.3)
    
    ax2.plot(steps, cl, 'b-', linewidth=1, label='CL')
    ax2.set_xlabel('Iteration/Step')
    ax2.set_ylabel('Lift Coefficient')
    ax2.legend()
    ax2.grid(True, alpha=0.3)
    
    fig.suptitle(f'{case_id} - Force History')
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, f'{case_id}_forces.png'), dpi=150)
    plt.close(fig)
    return True

def plot_surface_cp(result_dir, case_id, out_dir):
    surf_file = os.path.join(result_dir, 'surface.csv')
    if not os.path.exists(surf_file): return False
    
    rows = read_csv(surf_file)
    if not rows: return False
    
    x = [r.get('x', 0) for r in rows]
    cp = [r.get('cp', 0) for r in rows]
    # Sort by x position
    sorted_pairs = sorted(zip(x, cp))
    x_sorted = [p[0] for p in sorted_pairs]
    cp_sorted = [p[1] for p in sorted_pairs]
    
    fig, ax = plt.subplots(figsize=(8, 4))
    ax.plot(x_sorted, cp_sorted, 'b-', linewidth=1)
    ax.invert_yaxis()
    ax.set_xlabel('x/c')
    ax.set_ylabel('Cp')
    ax.set_title(f'{case_id} - Surface Pressure Coefficient')
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, f'{case_id}_cp.png'), dpi=150)
    plt.close(fig)
    return True

def plot_contour(result_dir, case_id, out_dir, variable='mach'):
    """Create contour plot from VTU field file."""
    vtu_file = os.path.join(result_dir, 'field_final.vtu')
    if not os.path.exists(vtu_file): return False
    
    # Parse VTU (simplified - read XML data arrays)
    with open(vtu_file) as f:
        content = f.read()
    
    # Extract point data
    import re
    import xml.etree.ElementTree as ET
    
    # Simple regex-based extraction for speed
    def extract_array(name):
        pattern = f'<DataArray[^>]*Name="{name}"[^>]*>(.*?)</DataArray>'
        match = re.search(pattern, content, re.DOTALL)
        if match:
            data = match.group(1).strip().split()
            return [float(x) for x in data]
        return None
    
    x = extract_array('Points')  # wrong - Points is in different format
    # Actually parse the XML properly
    try:
        root = ET.fromstring(content)
    except:
        print(f"XML parse error in {vtu_file}")
        return False
    
    ns = {}  # VTK doesn't use namespaces typically
    piece = root.find('.//Piece')
    if piece is None: return False
    
    n_points = int(piece.get('NumberOfPoints', 0))
    
    # Get points
    points_elem = piece.find('.//Points/DataArray')
    if points_elem is None: return False
    points_text = points_elem.text.strip().split()
    points = np.array([float(x) for x in points_text]).reshape(-1, 3)
    x_vals = points[:, 0]
    y_vals = points[:, 1]
    
    # Get target variable from CellData
    var_data = None
    for da in piece.findall('.//CellData/DataArray'):
        if da.get('Name') == variable:
            var_data = np.array([float(x) for x in da.text.strip().split()])
            break
    
    if var_data is None: return False
    
    # Filter out non-finite values
    mask = np.isfinite(var_data)
    if not np.any(mask): return False
    var_data = np.where(mask, var_data, 0.0)
    
    # Create triangulation
    triang = tri.Triangulation(x_vals, y_vals)
    
    fig, ax = plt.subplots(figsize=(10, 6))
    
    levels = 50
    vmin = np.percentile(var_data, 1)
    vmax = np.percentile(var_data, 99)
    
    tcf = ax.tricontourf(triang, var_data, levels=levels, cmap='jet', vmin=vmin, vmax=vmax)
    cbar = fig.colorbar(tcf, ax=ax)
    cbar.set_label(variable.capitalize())
    
    ax.set_xlabel('x')
    ax.set_ylabel('y')
    ax.set_title(f'{case_id} - {variable.capitalize()} Contours')
    ax.set_aspect('equal')
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, f'{case_id}_{variable}.png'), dpi=150)
    plt.close(fig)
    return True

def plot_all_for_case(result_dir, case_id, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    
    results = {}
    results['residuals'] = plot_residuals(result_dir, case_id, out_dir)
    results['forces'] = plot_forces(result_dir, case_id, out_dir)
    results['cp'] = plot_surface_cp(result_dir, case_id, out_dir)
    results['mach'] = plot_contour(result_dir, case_id, out_dir, 'mach')
    results['pressure'] = plot_contour(result_dir, case_id, out_dir, 'pressure')
    
    return results

def write_figure_manifest(out_dir, case_results):
    """Write figure_manifest.csv."""
    manifest = os.path.join(out_dir, 'figure_manifest.csv')
    with open(manifest, 'w') as f:
        f.write("figure_file,case_id,figure_type,variable,source_file,caption\n")
        for case_id, plot_types in case_results.items():
            for plot_type, ok in plot_types.items():
                if ok:
                    fig_file = f"{case_id}_{plot_type}.png"
                    if plot_type in ('residuals', 'forces', 'cp'):
                        var = plot_type
                        src = f"results/{case_id}/np1/{plot_type}.csv"
                    else:
                        var = plot_type
                        src = f"results/{case_id}/np1/field_final.vtu"
                    f.write(f"{fig_file},{case_id},{plot_type},{var},{src},{plot_type} plot for {case_id}\n")
    print(f"Figure manifest written: {manifest}")

if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('results_dir', help='Root results directory')
    p.add_argument('--output', default='report/figures', help='Output directory for figures')
    p.add_argument('--case', help='Process only this case')
    args = p.parse_args()
    
    os.makedirs(args.output, exist_ok=True)
    
    all_cases = {}
    results_root = Path(args.results_dir)
    
    for case_dir in sorted(results_root.iterdir()):
        if not case_dir.is_dir(): continue
        case_id = case_dir.name
        
        # Look for np1 or np subdirectory
        np1_dir = case_dir / 'np1'
        if not np1_dir.exists():
            np1_dir = case_dir  # might be direct
        
        if not os.listdir(np1_dir): continue
        
        print(f"Processing {case_id}...")
        res = plot_all_for_case(str(np1_dir), case_id, args.output)
        all_cases[case_id] = res
    
    write_figure_manifest(args.output, all_cases)
    print("Done!")
